#include "pipeline.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <image_process/gst_meta_v1.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "artifact.hpp"
#include "image_process.hpp"
#include "satellite/exit_codes.hpp"
#include "satellite/json_io.hpp"

#ifndef IMAGE_PROCESS_RUNTIME_MANIFEST_PATH
#define IMAGE_PROCESS_RUNTIME_MANIFEST_PATH "runtime-manifest.json"
#endif

namespace image_process {
namespace {

double timeval_seconds(const timeval& value) {
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_usec) / 1'000'000.0;
}

struct UsageSnapshot {
    double        cpu_seconds    = 0.0;
    std::uint64_t peak_rss_bytes = 0;
};

UsageSnapshot read_usage() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        throw ImageProcessError(satellite::EXIT_FATAL,
                                "failed to read process resource usage");
    }
#if defined(__APPLE__)
    const auto peak_rss_bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    const auto peak_rss_bytes =
        static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
    return {timeval_seconds(usage.ru_utime) + timeval_seconds(usage.ru_stime),
            peak_rss_bytes};
}

class ResourceSampler {
  public:
    ResourceSampler()
        : started_at_(std::chrono::steady_clock::now()),
          last_at_(started_at_),
          started_usage_(read_usage()),
          last_usage_(started_usage_) {}

    void sample() {
        const auto   now   = std::chrono::steady_clock::now();
        const auto   usage = read_usage();
        const double elapsed =
            std::chrono::duration<double>(now - last_at_).count();
        if (elapsed >= 0.01) {
            const double cpu_percent =
                100.0 * (usage.cpu_seconds - last_usage_.cpu_seconds) / elapsed;
            peak_cpu_percent_ = std::max(peak_cpu_percent_, cpu_percent);
        }
        last_at_    = now;
        last_usage_ = usage;
    }

    Json finish() {
        sample();
        const double wall_time_seconds =
            std::chrono::duration<double>(last_at_ - started_at_).count();
        const double cpu_time_seconds =
            last_usage_.cpu_seconds - started_usage_.cpu_seconds;
        const double average_cpu_percent =
            wall_time_seconds > 0.0
                ? 100.0 * cpu_time_seconds / wall_time_seconds
                : 0.0;
        return {{"wall_time_seconds", wall_time_seconds},
                {"cpu_time_seconds", cpu_time_seconds},
                {"average_cpu_percent", average_cpu_percent},
                {"peak_observed_cpu_percent", peak_cpu_percent_},
                {"peak_rss_bytes", last_usage_.peak_rss_bytes},
                {"sampling", "per_output_frame_and_bus_poll"}};
    }

  private:
    std::chrono::steady_clock::time_point started_at_;
    std::chrono::steady_clock::time_point last_at_;
    UsageSnapshot                         started_usage_;
    UsageSnapshot                         last_usage_;
    double                                peak_cpu_percent_ = 0.0;
};

class ScopedStdoutToStderr {
  public:
    ScopedStdoutToStderr() {
        std::fflush(stdout);
        saved_stdout_ = dup(STDOUT_FILENO);
        if (saved_stdout_ < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
            if (saved_stdout_ >= 0) { close(saved_stdout_); }
            throw ImageProcessError(
                satellite::EXIT_FATAL,
                "failed to isolate runtime logs from tools JSON stdout");
        }
    }

    ScopedStdoutToStderr(const ScopedStdoutToStderr&)            = delete;
    ScopedStdoutToStderr& operator=(const ScopedStdoutToStderr&) = delete;

    ~ScopedStdoutToStderr() {
        std::fflush(stdout);
        if (saved_stdout_ >= 0) {
            dup2(saved_stdout_, STDOUT_FILENO);
            close(saved_stdout_);
        }
    }

  private:
    int saved_stdout_ = -1;
};

void ensure_gstreamer_initialized() {
    static const bool initialized = [] {
        // MSF's default console sink writes to stdout. Disable it unless the
        // deployer explicitly chose otherwise so the tools JSON contract stays
        // machine-readable.
        if (g_getenv("MSF_LOG_CONSOLE") == nullptr) {
            g_setenv("MSF_LOG_CONSOLE", "0", FALSE);
        }
        GError* error = nullptr;
        if (!gst_init_check(nullptr, nullptr, &error)) {
            const std::string message =
                error == nullptr ? "unknown GStreamer initialization error"
                                 : error->message;
            if (error != nullptr) { g_error_free(error); }
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "GStreamer initialization failed: " + message);
        }

        // The standard install environment points this variable at the
        // image-process-owned runtime directory. Requests cannot inject paths;
        // only the process-level deployment environment can select a bundle.
        const gchar* configured_path =
            g_getenv("IMAGE_PROCESS_GST_PLUGIN_PATH");
        if (configured_path != nullptr && configured_path[0] != '\0') {
            gchar** paths =
                g_strsplit(configured_path, G_SEARCHPATH_SEPARATOR_S, -1);
            for (gchar** path = paths; *path != nullptr; ++path) {
                if ((*path)[0] != '\0') {
                    gst_registry_scan_path(gst_registry_get(), *path);
                }
            }
            g_strfreev(paths);
        }
        return true;
    }();
    (void)initialized;
}

void set_property(GstElement*        element,
                  const std::string& name,
                  const Json&        value) {
    GObject*    object = G_OBJECT(element);
    GParamSpec* spec =
        g_object_class_find_property(G_OBJECT_GET_CLASS(object), name.c_str());
    if (spec == nullptr || (spec->flags & G_PARAM_WRITABLE) == 0U) {
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "installed element does not expose writable "
                                "property: " +
                                    name);
    }

    const GType type = G_PARAM_SPEC_VALUE_TYPE(spec);
    if (type == G_TYPE_BOOLEAN && value.is_boolean()) {
        g_object_set(object, name.c_str(),
                     static_cast<gboolean>(value.get<bool>()), nullptr);
    }
    else if (type == G_TYPE_INT && value.is_number_integer()) {
        g_object_set(object, name.c_str(), value.get<gint>(), nullptr);
    }
    else if (type == G_TYPE_UINT && value.is_number_unsigned()) {
        g_object_set(object, name.c_str(), value.get<guint>(), nullptr);
    }
    else if (type == G_TYPE_INT64 && value.is_number_integer()) {
        g_object_set(object, name.c_str(), value.get<gint64>(), nullptr);
    }
    else if (type == G_TYPE_UINT64 && value.is_number_unsigned()) {
        g_object_set(object, name.c_str(), value.get<guint64>(), nullptr);
    }
    else if (type == G_TYPE_DOUBLE && value.is_number()) {
        g_object_set(object, name.c_str(), value.get<gdouble>(), nullptr);
    }
    else if (type == G_TYPE_FLOAT && value.is_number()) {
        g_object_set(object, name.c_str(), value.get<gfloat>(), nullptr);
    }
    else if (type == G_TYPE_STRING && value.is_string()) {
        const std::string text = value.get<std::string>();
        g_object_set(object, name.c_str(), text.c_str(), nullptr);
    }
    else if (G_TYPE_IS_ENUM(type) && value.is_string()) {
        GEnumClass*       enum_class = G_ENUM_CLASS(g_type_class_ref(type));
        const std::string text       = value.get<std::string>();
        const GEnumValue* enum_value =
            g_enum_get_value_by_nick(enum_class, text.c_str());
        if (enum_value == nullptr) {
            g_type_class_unref(enum_class);
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "runtime profile enum nick is not supported: " + name + "=" +
                    text);
        }
        g_object_set(object, name.c_str(), enum_value->value, nullptr);
        g_type_class_unref(enum_class);
    }
    else if (G_TYPE_IS_FLAGS(type) && value.is_string()) {
        GFlagsClass*       flags_class = G_FLAGS_CLASS(g_type_class_ref(type));
        const std::string  text        = value.get<std::string>();
        const GFlagsValue* flags_value =
            g_flags_get_value_by_nick(flags_class, text.c_str());
        if (flags_value == nullptr) {
            g_type_class_unref(flags_class);
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "runtime profile flags nick is not supported: " + name + "=" +
                    text);
        }
        g_object_set(object, name.c_str(), flags_value->value, nullptr);
        g_type_class_unref(flags_class);
    }
    else {
        throw ImageProcessError(
            satellite::EXIT_DEPENDENCY,
            "runtime profile property type does not match installed element: " +
                name);
    }
}

void apply_properties(GstElement* element, const Json& properties) {
    for (const auto& item : properties.items()) {
        set_property(element, item.key(), item.value());
    }
}

Json factory_provenance(const std::string& factory_name) {
    GstElementFactory* factory = gst_element_factory_find(factory_name.c_str());
    if (factory == nullptr) { return Json::object(); }
    const gchar* plugin_name =
        gst_plugin_feature_get_plugin_name(GST_PLUGIN_FEATURE(factory));
    Json result = {{"factory", factory_name},
                   {"plugin", plugin_name == nullptr ? "" : plugin_name}};
    if (plugin_name != nullptr) {
        GstPlugin* plugin =
            gst_registry_find_plugin(gst_registry_get(), plugin_name);
        if (plugin != nullptr) {
            result["plugin_version"] = gst_plugin_get_version(plugin);
            result["plugin_license"] = gst_plugin_get_license(plugin);
            result["plugin_origin"]  = gst_plugin_get_origin(plugin);
            gst_object_unref(plugin);
        }
    }
    gst_object_unref(factory);
    return result;
}

Json runtime_contract_provenance() {
    std::filesystem::path path;
    if (const char* data_root = std::getenv("IMAGE_PROCESS_DATA_ROOT")) {
        path = std::filesystem::path(data_root) / "image-process" / "runtime" /
               "runtime-manifest.json";
    }
    else { path = IMAGE_PROCESS_RUNTIME_MANIFEST_PATH; }
    Json manifest;
    try {
        manifest = satellite::read_json_file(path);
    } catch (const std::exception& error) {
        throw ImageProcessError(
            satellite::EXIT_DEPENDENCY,
            "image-process runtime manifest is unavailable: " +
                std::string(error.what()));
    }
    if (manifest.value("component", "") != "image-process" ||
        manifest.value("meta_abi", "") != "image-process.gst-meta.v1" ||
        !manifest.contains("component_revision") ||
        !manifest.contains("source_provenance_sha256") ||
        !manifest.contains("build_features")) {
        throw ImageProcessError(
            satellite::EXIT_DEPENDENCY,
            "image-process runtime manifest contract is incompatible");
    }
    return {
        {"component", manifest.at("component")},
        {"component_version", manifest.at("component_version")},
        {"component_revision", manifest.at("component_revision")},
        {"meta_abi", manifest.at("meta_abi")},
        {"source_provenance_sha256", manifest.at("source_provenance_sha256")},
        {"runtime_manifest_sha256", sha256_file(path)},
        {"build_features", manifest.at("build_features")},
    };
}

Json finite_or_null(float value) {
    return std::isfinite(value) ? Json(value) : Json(nullptr);
}

Json float_triplet(const float (&values)[3]) {
    return Json::array({finite_or_null(values[0]), finite_or_null(values[1]),
                        finite_or_null(values[2])});
}

Json normalize_cdg00_parameter(const IpCdg00SampleV1& parameter) {
    return {
        {"channel_id", parameter.channel_id},
        {"strip_number", parameter.strip_number},
        {"row_number", parameter.row_number},
        {"time_sync_status", parameter.time_sync_status},
        {"camera_time",
         {{"scale", "camera"},
          {"seconds", parameter.camera_seconds},
          {"microseconds", parameter.camera_microseconds}}},
        {"exposure", {{"value", parameter.exposure_time_ns}, {"unit", "ns"}}},
        {"gps_time",
         {{"scale", "GPS"},
          {"week", parameter.gps_week},
          {"seconds", parameter.gps_seconds}}},
        {"lla",
         {{"values", float_triplet(parameter.lla)},
          {"units", Json::array({"source", "source", "source"})},
          {"normalized", false}}},
        {"velocity",
         {{"values", float_triplet(parameter.velocity)},
          {"frame", "source"},
          {"units", Json::array({"source", "source", "source"})},
          {"normalized", false}}},
        {"attitude",
         {{"values", float_triplet(parameter.attitude)},
          {"order", "roll-pitch-yaw_3-2-1"},
          {"frame", "source"},
          {"units", Json::array({"source", "source", "source"})},
          {"normalized", false}}},
        {"source_convention", "msf.cdg00"}};
}

IpCdg00MetaV1 read_cdg00_meta(GstBuffer* buffer) {
    bool     api_present = false;
    gpointer state       = nullptr;
    while (GstMeta* meta = gst_buffer_iterate_meta(buffer, &state)) {
        const gchar* api_name = g_type_name(meta->info->api);
        if (api_name != nullptr && std::string(api_name) == "meta_cdg00_api") {
            api_present = true;
            break;
        }
    }
    IpCdg00MetaV1 value{};
    if (ip_buffer_get_cdg00_meta(buffer, &value)) { return value; }
    if (api_present) {
        throw ImageProcessError(
            satellite::EXIT_DEPENDENCY,
            "CDG0.0 metadata ABI is not image-process.gst-meta.v1");
    }
    throw ImageProcessError(
        satellite::EXIT_DEPENDENCY,
        "CDG00Src output is missing required meta_cdg00_api metadata");
}

Json normalize_cdg00_meta(GstBuffer* buffer) {
    const IpCdg00MetaV1 value = read_cdg00_meta(buffer);
    return {{"abi", "image-process.cdg00.meta-v1"},
            {"window_start", normalize_cdg00_parameter(value.window_start)},
            {"window_end", normalize_cdg00_parameter(value.window_end)}};
}

Json concise_cdg00_metadata(GstBuffer* buffer, std::size_t frame_id) {
    const IpCdg00MetaV1 value = read_cdg00_meta(buffer);
    const auto&         start = value.window_start;
    return {{"frame_id", frame_id},
            {"gps_time",
             {{"scale", "GPS"},
              {"week", start.gps_week},
              {"seconds", start.gps_seconds}}},
            {"lla", float_triplet(start.lla)},
            {"rpy", float_triplet(start.attitude)},
            {"velocity", float_triplet(start.velocity)}};
}

struct GroundProbeContext {
    std::mutex         mutex;
    std::size_t        frame_count = 0;
    std::size_t        max_frames  = 0;
    MetadataConsumer   consumer;
    std::exception_ptr error;
    Json               first_metadata;
    Json               last_metadata;
};

GstPadProbeReturn collect_ground_metadata(GstPad*,
                                          GstPadProbeInfo* info,
                                          gpointer         user_data) {
    if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0U) {
        return GST_PAD_PROBE_OK;
    }
    auto* context = static_cast<GroundProbeContext*>(user_data);
    std::lock_guard<std::mutex> lock(context->mutex);
    if (context->error != nullptr) { return GST_PAD_PROBE_DROP; }
    try {
        if (context->frame_count >= context->max_frames) {
            throw ImageProcessError(
                satellite::EXIT_RETRYABLE,
                "pipeline exceeded the installed frame limit before EOS");
        }
        GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        Json metadata = concise_cdg00_metadata(buffer, context->frame_count);
        context->consumer(metadata);
        if (context->frame_count == 0U) { context->first_metadata = metadata; }
        context->last_metadata = metadata;
        ++context->frame_count;
    } catch (...) { context->error = std::current_exception(); }
    return context->error == nullptr ? GST_PAD_PROBE_OK : GST_PAD_PROBE_DROP;
}

void rethrow_probe_error(GroundProbeContext& context) {
    std::lock_guard<std::mutex> lock(context.mutex);
    if (context.error != nullptr) { std::rethrow_exception(context.error); }
}

void require_factory(const std::string& factory_name) {
    GstElementFactory* factory = gst_element_factory_find(factory_name.c_str());
    if (factory == nullptr) {
        throw ImageProcessError(
            satellite::EXIT_DEPENDENCY,
            "required GStreamer factory not found: " + factory_name);
    }
    gst_object_unref(factory);
}

Json sample_metadata(GstSample* sample, std::size_t index) {
    GstBuffer* buffer   = gst_sample_get_buffer(sample);
    GstCaps*   caps     = gst_sample_get_caps(sample);
    Json       metadata = {{"index", index}, {"meta_apis", Json::array()}};

    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        metadata["pts_ns"] = GST_BUFFER_PTS(buffer);
    }
    else { metadata["pts_ns"] = nullptr; }
    if (GST_BUFFER_DURATION_IS_VALID(buffer)) {
        metadata["duration_ns"] = GST_BUFFER_DURATION(buffer);
    }
    else { metadata["duration_ns"] = nullptr; }

    GstVideoInfo info;
    if (caps != nullptr && gst_video_info_from_caps(&info, caps)) {
        metadata["video"] = {
            {"width", GST_VIDEO_INFO_WIDTH(&info)},
            {"height", GST_VIDEO_INFO_HEIGHT(&info)},
            {"format",
             gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info))},
        };
    }
    else if (caps != nullptr) {
        gchar* caps_text = gst_caps_to_string(caps);
        metadata["caps"] = caps_text == nullptr ? "" : caps_text;
        if (caps_text != nullptr) { g_free(caps_text); }
    }

    gpointer state = nullptr;
    while (GstMeta* meta = gst_buffer_iterate_meta(buffer, &state)) {
        const gchar*      api_name = g_type_name(meta->info->api);
        const std::string api      = api_name == nullptr ? "" : api_name;
        metadata["meta_apis"].push_back(api);
        if (api == "meta_cdg00_api") {
            metadata["cdg00"] = normalize_cdg00_meta(buffer);
        }
    }
    return metadata;
}

ProcessedFrame copy_sample(GstSample* sample, std::size_t index) {
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        throw ImageProcessError(satellite::EXIT_RETRYABLE,
                                "failed to map output GstBuffer");
    }
    ProcessedFrame frame;
    frame.bytes.assign(map.data, map.data + map.size);
    gst_buffer_unmap(buffer, &map);
    frame.metadata               = sample_metadata(sample, index);
    frame.metadata["size_bytes"] = frame.bytes.size();
    return frame;
}

}  // namespace

Json make_pipeline_plan(const Json& profile) {
    if (profile.value("output_mode", "") == "product_text") {
        return {{"source",
                 {{"factory", profile.at("source").at("factory")},
                  {"role", "sensor_source"},
                  {"properties", profile.at("source").at("properties")}}},
                {"geometry",
                 {{"factory", profile.at("geometry").at("factory")},
                  {"role", "geometry"},
                  {"properties", profile.at("geometry").at("properties")}}},
                {"filter",
                 {{"factory", profile.at("filter").at("factory")},
                  {"role", profile.at("filter").at("role")},
                  {"properties", profile.at("filter").at("properties")}}},
                {"sink",
                 {{"factory", "ImageProcessTextSink"},
                  {"role", "product_text_sink"}}},
                {"output_mode", "product_text"},
                {"evidence_class", profile.at("evidence_class")},
                {"filter_backend", profile.value("filter_backend", "mock")}};
    }
    if (profile.value("output_mode", "") == "ground_cdg00") {
        Json video_plan = {
            {"convert", profile.at("video").at("convert")},
            {"scale", profile.at("video").at("scale")},
            {"caps", profile.at("video").at("caps")},
            {"encoder", profile.at("video").at("encoder")},
            {"muxer", profile.at("video").at("muxer")},
            {"sink", {{"factory", "filesink"}, {"path", "video.ogv"}}}};
        if (profile.at("video").contains("parser")) {
            video_plan["parser"] = profile.at("video").at("parser");
        }
        return {
            {"source",
             {{"factory", profile.at("source").at("factory")},
              {"role", "sensor_source"},
              {"properties", profile.at("source").at("properties")}}},
            {"metadata_probe",
             {{"factory", profile.at("filter").at("factory")},
              {"role", "metadata_only"},
              {"contract",
               {{"fields", {"frame_id", "lla", "rpy", "velocity", "gps_time"}},
                {"sample_point", "window_start"},
                {"gps_time_scale", "GPS"},
                {"lla_order", "longitude_latitude_altitude"},
                {"rpy_order", "roll_pitch_yaw_3-2-1"},
                {"velocity_order", "x_y_z"},
                {"source_convention", "msf.cdg00"},
                {"frame", "source"},
                {"units", "source"},
                {"zero_values_do_not_imply_valid", true}}}}},
            {"video", video_plan},
            {"output_mode", "ground_cdg00"},
            {"evidence_class", profile.at("evidence_class")}};
    }
    Json plan = {{"source",
                  {{"factory", profile.at("source").at("factory")},
                   {"role", "sensor_source"},
                   {"properties", profile.at("source").at("properties")}}},
                 {"normalization", {{"role", "raw_video_caps"}}},
                 {"filter",
                  {{"factory", profile.at("filter").at("factory")},
                   {"role", profile.at("filter").at("role")},
                   {"properties", profile.at("filter").at("properties")}}},
                 {"sink",
                  {{"factory", "appsink"},
                   {"role", "bounded_artifact_sink"},
                   {"max_buffers", 2},
                   {"wait_on_eos", false}}},
                 {"evidence_class", profile.at("evidence_class")}};
    if (profile.contains("output_mode")) {
        plan["output_mode"] = profile.at("output_mode");
    }
    return plan;
}

void preflight_pipeline(const Json& profile) {
    ensure_gstreamer_initialized();
    require_factory(profile.at("source").at("factory").get<std::string>());
    require_factory(profile.at("filter").at("factory").get<std::string>());
    if (profile.value("output_mode", "") == "product_text") {
        require_factory(
            profile.at("geometry").at("factory").get<std::string>());
        require_factory("ImageProcessTextSink");
        return;
    }
    if (profile.value("output_mode", "") == "ground_cdg00") {
        for (const char* section : {"convert", "scale", "encoder", "muxer"}) {
            require_factory(profile.at("video")
                                .at(section)
                                .at("factory")
                                .get<std::string>());
        }
        if (profile.at("video").contains("parser")) {
            require_factory(profile.at("video")
                                .at("parser")
                                .at("factory")
                                .get<std::string>());
        }
        require_factory("capsfilter");
        require_factory("filesink");
    }
    else { require_factory("appsink"); }
}

PipelineResult run_pipeline(const Json&                  profile,
                            const std::filesystem::path& input_path,
                            std::size_t                  max_frames,
                            const FrameConsumer&         consumer) {
    // Third-party elements may log directly to stdout while processing. Keep
    // those diagnostics on stderr so the CLI emits exactly one JSON document.
    ScopedStdoutToStderr runtime_log_guard;
    preflight_pipeline(profile);
    const std::string source_name =
        profile.at("source").at("factory").get<std::string>();
    const std::string filter_name =
        profile.at("filter").at("factory").get<std::string>();

    GstElement* pipeline = gst_pipeline_new("image-process-pipeline");
    GstElement* source =
        gst_element_factory_make(source_name.c_str(), "source");
    GstElement* filter =
        gst_element_factory_make(filter_name.c_str(), "filter");
    GstElement* sink = gst_element_factory_make("appsink", "artifact-sink");
    if (pipeline == nullptr || source == nullptr || filter == nullptr ||
        sink == nullptr) {
        if (pipeline != nullptr) { gst_object_unref(pipeline); }
        if (source != nullptr) { gst_object_unref(source); }
        if (filter != nullptr) { gst_object_unref(filter); }
        if (sink != nullptr) { gst_object_unref(sink); }
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "failed to instantiate GStreamer pipeline");
    }

    gst_bin_add_many(GST_BIN(pipeline), source, filter, sink, nullptr);

    auto cleanup = [&pipeline] {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    };

    try {
        apply_properties(source, profile.at("source").at("properties"));
        apply_properties(filter, profile.at("filter").at("properties"));
        if (profile.at("source").contains("input_path_property")) {
            if (input_path.empty()) {
                throw ImageProcessError(satellite::EXIT_VALIDATION,
                                        "runtime profile requires input.path");
            }
            set_property(source,
                         profile.at("source")
                             .at("input_path_property")
                             .get<std::string>(),
                         input_path.string());
        }

        GstAppSink* app_sink = GST_APP_SINK(sink);
        gst_app_sink_set_emit_signals(app_sink, false);
        gst_app_sink_set_max_buffers(app_sink, 2);
        gst_app_sink_set_wait_on_eos(app_sink, false);

        if (!gst_element_link_many(source, filter, sink, nullptr)) {
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "installed source and filter cannot negotiate a pipeline");
        }

        const GstStateChangeReturn state =
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (state == GST_STATE_CHANGE_FAILURE) {
            throw ImageProcessError(satellite::EXIT_RETRYABLE,
                                    "pipeline failed to enter PLAYING state");
        }

        PipelineResult  result;
        ResourceSampler resource_sampler;
        GstBus*         bus = gst_element_get_bus(pipeline);
        const auto      deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(profile.at("timeout_sec").get<unsigned int>());
        bool done = false;
        while (!done) {
            GstSample* sample =
                gst_app_sink_try_pull_sample(app_sink, 100 * GST_MSECOND);
            if (sample != nullptr) {
                if (result.frame_count >= max_frames) {
                    gst_sample_unref(sample);
                    gst_object_unref(bus);
                    throw ImageProcessError(
                        satellite::EXIT_RETRYABLE,
                        "pipeline exceeded the installed frame limit before "
                        "EOS");
                }
                ProcessedFrame frame = copy_sample(sample, result.frame_count);
                gst_sample_unref(sample);
                consumer(frame);
                if (result.frame_count == 0U) {
                    result.first_frame_metadata = frame.metadata;
                }
                result.last_frame_metadata = frame.metadata;
                ++result.frame_count;
                resource_sampler.sample();
                continue;
            }

            GstMessage* message = gst_bus_pop_filtered(
                bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR |
                                                 GST_MESSAGE_EOS));
            if (message != nullptr) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    GError* error = nullptr;
                    gchar*  debug = nullptr;
                    gst_message_parse_error(message, &error, &debug);
                    const std::string detail = error == nullptr
                                                   ? "unknown pipeline error"
                                                   : error->message;
                    if (error != nullptr) { g_error_free(error); }
                    if (debug != nullptr) { g_free(debug); }
                    gst_message_unref(message);
                    gst_object_unref(bus);
                    throw ImageProcessError(
                        satellite::EXIT_RETRYABLE,
                        "GStreamer pipeline error: " + detail);
                }
                done = true;
                gst_message_unref(message);
            }
            resource_sampler.sample();
            if (std::chrono::steady_clock::now() >= deadline) {
                gst_object_unref(bus);
                throw ImageProcessError(satellite::EXIT_RETRYABLE,
                                        "GStreamer pipeline timed out");
            }
        }
        gst_object_unref(bus);

        result.provenance     = {{"gstreamer_version", gst_version_string()},
                                 {"runtime", runtime_contract_provenance()},
                                 {"source", factory_provenance(source_name)},
                                 {"filter", factory_provenance(filter_name)},
                                 {"sink", factory_provenance("appsink")},
                                 {"filter_role", profile.at("filter").at("role")},
                                 {"evidence_class", profile.at("evidence_class")}};
        result.resource_usage = resource_sampler.finish();
        cleanup();
        return result;
    } catch (...) {
        cleanup();
        throw;
    }
}

PipelineResult run_ground_cdg00_pipeline(
    const Json&                  profile,
    const std::filesystem::path& input_path,
    const std::filesystem::path& video_partial,
    std::size_t                  max_frames,
    const MetadataConsumer&      metadata_consumer) {
    ScopedStdoutToStderr runtime_log_guard;
    preflight_pipeline(profile);

    const Json&       video = profile.at("video");
    const std::string source_name =
        profile.at("source").at("factory").get<std::string>();
    const std::string probe_name =
        profile.at("filter").at("factory").get<std::string>();
    const std::string convert_name =
        video.at("convert").at("factory").get<std::string>();
    const std::string scale_name =
        video.at("scale").at("factory").get<std::string>();
    const std::string encoder_name =
        video.at("encoder").at("factory").get<std::string>();
    const std::string parser_name =
        video.contains("parser")
            ? video.at("parser").at("factory").get<std::string>()
            : "";
    const std::string muxer_name =
        video.at("muxer").at("factory").get<std::string>();

    GstElement* pipeline = gst_pipeline_new("image-process-cdg00-ground");
    GstElement* source =
        gst_element_factory_make(source_name.c_str(), "source");
    GstElement* probe =
        gst_element_factory_make(probe_name.c_str(), "meta-probe");
    GstElement* convert =
        gst_element_factory_make(convert_name.c_str(), "video-convert");
    GstElement* scale =
        gst_element_factory_make(scale_name.c_str(), "video-scale");
    GstElement* caps_filter =
        gst_element_factory_make("capsfilter", "video-caps");
    GstElement* encoder =
        gst_element_factory_make(encoder_name.c_str(), "video-encoder");
    GstElement* parser =
        parser_name.empty()
            ? nullptr
            : gst_element_factory_make(parser_name.c_str(), "stream-parser");
    GstElement* muxer =
        gst_element_factory_make(muxer_name.c_str(), "video-muxer");
    GstElement* sink = gst_element_factory_make("filesink", "video-sink");
    const std::array<GstElement*, 8> elements = {
        pipeline, source, probe, convert, scale, caps_filter, encoder, muxer};
    if (std::any_of(elements.begin(), elements.end(),
                    [](GstElement* element) { return element == nullptr; }) ||
        sink == nullptr || (!parser_name.empty() && parser == nullptr)) {
        if (pipeline != nullptr) { gst_object_unref(pipeline); }
        for (GstElement* element : {source, probe, convert, scale, caps_filter,
                                    encoder, parser, muxer, sink}) {
            if (element != nullptr) { gst_object_unref(element); }
        }
        throw ImageProcessError(
            satellite::EXIT_DEPENDENCY,
            "failed to instantiate approved CDG0.0 ground pipeline");
    }

    if (parser != nullptr) {
        gst_bin_add_many(GST_BIN(pipeline), source, probe, convert, scale,
                         caps_filter, encoder, parser, muxer, sink, nullptr);
    }
    else {
        gst_bin_add_many(GST_BIN(pipeline), source, probe, convert, scale,
                         caps_filter, encoder, muxer, sink, nullptr);
    }
    GroundProbeContext probe_context;
    probe_context.max_frames = max_frames;
    probe_context.consumer   = metadata_consumer;
    GstPad* probe_pad        = gst_element_get_static_pad(probe, "src");
    if (probe_pad == nullptr) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "metadata probe has no source pad");
    }
    const gulong probe_id =
        gst_pad_add_probe(probe_pad, GST_PAD_PROBE_TYPE_BUFFER,
                          collect_ground_metadata, &probe_context, nullptr);
    gst_object_unref(probe_pad);
    if (probe_id == 0U) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "failed to install metadata pad probe");
    }

    auto cleanup = [&pipeline, probe, probe_id] {
        GstPad* pad = gst_element_get_static_pad(probe, "src");
        if (pad != nullptr) {
            if (probe_id != 0U) { gst_pad_remove_probe(pad, probe_id); }
            gst_object_unref(pad);
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    };

    try {
        apply_properties(source, profile.at("source").at("properties"));
        apply_properties(probe, profile.at("filter").at("properties"));
        set_property(
            source,
            profile.at("source").at("input_path_property").get<std::string>(),
            input_path.string());
        apply_properties(convert, video.at("convert").at("properties"));
        apply_properties(scale, video.at("scale").at("properties"));
        apply_properties(encoder, video.at("encoder").at("properties"));
        if (parser != nullptr) {
            apply_properties(parser, video.at("parser").at("properties"));
        }
        apply_properties(muxer, video.at("muxer").at("properties"));
        set_property(sink, "location", video_partial.string());
        set_property(sink, "sync", false);

        const Json& caps_json = video.at("caps");
        GstCaps*    caps      = gst_caps_new_simple(
            "video/x-raw", "format", G_TYPE_STRING,
            caps_json.at("format").get<std::string>().c_str(), "width",
            G_TYPE_INT, caps_json.at("width").get<int>(), "height", G_TYPE_INT,
            caps_json.at("height").get<int>(), "framerate", GST_TYPE_FRACTION,
            caps_json.at("framerate_num").get<int>(),
            caps_json.at("framerate_den").get<int>(), nullptr);
        g_object_set(G_OBJECT(caps_filter), "caps", caps, nullptr);
        gst_caps_unref(caps);

        const bool linked =
            parser != nullptr
                ? gst_element_link_many(source, probe, convert, scale,
                                        caps_filter, encoder, parser, muxer,
                                        sink, nullptr)
                : gst_element_link_many(source, probe, convert, scale,
                                        caps_filter, encoder, muxer, sink,
                                        nullptr);
        if (!linked) {
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "approved CDG0.0 ground elements cannot negotiate a pipeline");
        }
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
            GST_STATE_CHANGE_FAILURE) {
            throw ImageProcessError(satellite::EXIT_RETRYABLE,
                                    "CDG0.0 ground pipeline failed to enter "
                                    "PLAYING state");
        }

        ResourceSampler resource_sampler;
        GstBus*         bus = gst_element_get_bus(pipeline);
        const auto      deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(profile.at("timeout_sec").get<unsigned int>());
        bool done = false;
        while (!done) {
            GstMessage* message = gst_bus_timed_pop_filtered(
                bus, 100 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR |
                                            GST_MESSAGE_EOS));
            rethrow_probe_error(probe_context);
            if (message != nullptr) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    GError* error = nullptr;
                    gchar*  debug = nullptr;
                    gst_message_parse_error(message, &error, &debug);
                    const std::string detail = error == nullptr
                                                   ? "unknown pipeline error"
                                                   : error->message;
                    if (error != nullptr) { g_error_free(error); }
                    if (debug != nullptr) { g_free(debug); }
                    gst_message_unref(message);
                    gst_object_unref(bus);
                    throw ImageProcessError(
                        satellite::EXIT_RETRYABLE,
                        "GStreamer CDG0.0 ground pipeline error: " + detail);
                }
                done = true;
                gst_message_unref(message);
            }
            resource_sampler.sample();
            if (std::chrono::steady_clock::now() >= deadline) {
                gst_object_unref(bus);
                throw ImageProcessError(
                    satellite::EXIT_RETRYABLE,
                    "GStreamer CDG0.0 ground pipeline timed out");
            }
        }
        gst_object_unref(bus);
        rethrow_probe_error(probe_context);

        PipelineResult result;
        {
            std::lock_guard<std::mutex> lock(probe_context.mutex);
            result.frame_count          = probe_context.frame_count;
            result.first_frame_metadata = probe_context.first_metadata;
            result.last_frame_metadata  = probe_context.last_metadata;
        }
        result.provenance = {{"gstreamer_version", gst_version_string()},
                             {"runtime", runtime_contract_provenance()},
                             {"source", factory_provenance(source_name)},
                             {"metadata_probe", factory_provenance(probe_name)},
                             {"convert", factory_provenance(convert_name)},
                             {"scale", factory_provenance(scale_name)},
                             {"encoder", factory_provenance(encoder_name)},
                             {"muxer", factory_provenance(muxer_name)},
                             {"evidence_class", profile.at("evidence_class")}};
        if (!parser_name.empty()) {
            result.provenance["parser"] = factory_provenance(parser_name);
        }
        result.resource_usage = resource_sampler.finish();
        cleanup();
        return result;
    } catch (...) {
        cleanup();
        throw;
    }
}

[[noreturn]] void throw_gst_message_error(GstMessage* message,
                                          const char* prefix) {
    GError* error = nullptr;
    gchar*  debug = nullptr;
    gst_message_parse_error(message, &error, &debug);
    const std::string detail =
        error == nullptr ? "unknown pipeline error" : error->message;
    const GQuark domain = error == nullptr ? 0 : error->domain;
    if (error != nullptr) { g_error_free(error); }
    if (debug != nullptr) { g_free(debug); }
    gst_message_unref(message);
    if (domain == GST_RESOURCE_ERROR) {
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                std::string(prefix) + detail);
    }
    if (domain == GST_STREAM_ERROR) {
        throw ImageProcessError(satellite::EXIT_FATAL,
                                std::string(prefix) + detail);
    }
    throw ImageProcessError(satellite::EXIT_RETRYABLE,
                            std::string(prefix) + detail);
}

struct FrameCountContext {
    std::mutex         mutex;
    std::size_t        frame_count = 0;
    std::size_t        max_frames  = 0;
    std::exception_ptr error;
};

GstPadProbeReturn count_product_frames(GstPad*,
                                       GstPadProbeInfo* info,
                                       gpointer         user_data) {
    if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0U) {
        return GST_PAD_PROBE_OK;
    }
    auto* context = static_cast<FrameCountContext*>(user_data);
    std::lock_guard<std::mutex> lock(context->mutex);
    if (context->error != nullptr) { return GST_PAD_PROBE_DROP; }
    try {
        if (context->frame_count >= context->max_frames) {
            throw ImageProcessError(
                satellite::EXIT_RETRYABLE,
                "pipeline exceeded the installed frame limit before EOS");
        }
        ++context->frame_count;
    } catch (...) { context->error = std::current_exception(); }
    return context->error == nullptr ? GST_PAD_PROBE_OK : GST_PAD_PROBE_DROP;
}

void rethrow_frame_error(FrameCountContext& context) {
    std::lock_guard<std::mutex> lock(context.mutex);
    if (context.error != nullptr) { std::rethrow_exception(context.error); }
}

PipelineResult run_product_text_pipeline(
    const Json&                  profile,
    const std::filesystem::path& input_path,
    std::size_t                  max_frames,
    const Json&                  sink_properties) {
    ScopedStdoutToStderr runtime_log_guard;
    preflight_pipeline(profile);

    const std::string source_name =
        profile.at("source").at("factory").get<std::string>();
    const std::string geometry_name =
        profile.at("geometry").at("factory").get<std::string>();
    const std::string filter_name =
        profile.at("filter").at("factory").get<std::string>();
    const std::string sink_name = "ImageProcessTextSink";

    GstElement* pipeline = gst_pipeline_new("image-process-product-text");
    GstElement* source =
        gst_element_factory_make(source_name.c_str(), "source");
    GstElement* geometry =
        gst_element_factory_make(geometry_name.c_str(), "geometry");
    GstElement* filter =
        gst_element_factory_make(filter_name.c_str(), "filter");
    GstElement* sink = gst_element_factory_make(sink_name.c_str(), "text-sink");
    if (pipeline == nullptr || source == nullptr || geometry == nullptr ||
        filter == nullptr || sink == nullptr) {
        if (pipeline != nullptr) { gst_object_unref(pipeline); }
        for (GstElement* element : {source, geometry, filter, sink}) {
            if (element != nullptr) { gst_object_unref(element); }
        }
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "failed to instantiate product text pipeline");
    }

    gst_bin_add_many(GST_BIN(pipeline), source, geometry, filter, sink,
                     nullptr);
    FrameCountContext probe_context;
    probe_context.max_frames = max_frames;
    GstPad* probe_pad        = gst_element_get_static_pad(sink, "sink");
    if (probe_pad == nullptr) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "product text sink has no sink pad");
    }
    const gulong probe_id =
        gst_pad_add_probe(probe_pad, GST_PAD_PROBE_TYPE_BUFFER,
                          count_product_frames, &probe_context, nullptr);
    gst_object_unref(probe_pad);
    if (probe_id == 0U) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        throw ImageProcessError(satellite::EXIT_DEPENDENCY,
                                "failed to install product text pad probe");
    }

    auto cleanup = [&pipeline, sink, probe_id] {
        GstPad* pad = gst_element_get_static_pad(sink, "sink");
        if (pad != nullptr) {
            if (probe_id != 0U) { gst_pad_remove_probe(pad, probe_id); }
            gst_object_unref(pad);
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    };

    try {
        apply_properties(source, profile.at("source").at("properties"));
        apply_properties(geometry, profile.at("geometry").at("properties"));
        apply_properties(filter, profile.at("filter").at("properties"));
        apply_properties(sink, sink_properties);
        if (profile.at("source").contains("input_path_property")) {
            if (input_path.empty()) {
                throw ImageProcessError(satellite::EXIT_VALIDATION,
                                        "runtime profile requires input.path");
            }
            set_property(source,
                         profile.at("source")
                             .at("input_path_property")
                             .get<std::string>(),
                         input_path.string());
        }
        if (!gst_element_link_many(source, geometry, filter, sink, nullptr)) {
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "product text elements cannot negotiate a pipeline");
        }
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
            GST_STATE_CHANGE_FAILURE) {
            throw ImageProcessError(
                satellite::EXIT_RETRYABLE,
                "product text pipeline failed to enter PLAYING state");
        }

        ResourceSampler resource_sampler;
        GstBus*         bus = gst_element_get_bus(pipeline);
        const auto      deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(profile.at("timeout_sec").get<unsigned int>());
        bool done = false;
        while (!done) {
            GstMessage* message = gst_bus_timed_pop_filtered(
                bus, 100 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR |
                                            GST_MESSAGE_EOS));
            rethrow_frame_error(probe_context);
            if (message != nullptr) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    gst_object_unref(bus);
                    throw_gst_message_error(
                        message, "GStreamer product text pipeline error: ");
                }
                done = true;
                gst_message_unref(message);
            }
            resource_sampler.sample();
            if (std::chrono::steady_clock::now() >= deadline) {
                gst_object_unref(bus);
                throw ImageProcessError(
                    satellite::EXIT_RETRYABLE,
                    "GStreamer product text pipeline timed out");
            }
        }
        gst_object_unref(bus);
        rethrow_frame_error(probe_context);

        PipelineResult result;
        {
            std::lock_guard<std::mutex> lock(probe_context.mutex);
            result.frame_count = probe_context.frame_count;
        }
        result.provenance = {
            {"gstreamer_version", gst_version_string()},
            {"runtime", runtime_contract_provenance()},
            {"source", factory_provenance(source_name)},
            {"geometry", factory_provenance(geometry_name)},
            {"filter", factory_provenance(filter_name)},
            {"sink", factory_provenance(sink_name)},
            {"filter_role", profile.at("filter").at("role")},
            {"filter_backend", profile.value("filter_backend", "mock")},
            {"filter_factory", filter_name},
            {"evidence_class", profile.at("evidence_class")}};
        result.resource_usage = resource_sampler.finish();
        cleanup();
        return result;
    } catch (...) {
        cleanup();
        throw;
    }
}

}  // namespace image_process
