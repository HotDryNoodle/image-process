#include "pipeline.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "image_process.hpp"
#include "satellite/exit_codes.hpp"

namespace image_process {
namespace {

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

        // Deployment owns the runtime bundle location. Requests cannot inject
        // paths; image-process only scans the process-level allowlisted path.
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
        const gchar* api_name = g_type_name(meta->info->api);
        metadata["meta_apis"].push_back(api_name == nullptr ? "" : api_name);
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
    return {{"source",
             {{"factory", profile.at("source").at("factory")},
              {"role", "sensor_source"}}},
            {"normalization", {{"role", "raw_video_caps"}}},
            {"filter",
             {{"factory", profile.at("filter").at("factory")},
              {"role", profile.at("filter").at("role")}}},
            {"sink",
             {{"factory", "appsink"},
              {"role", "bounded_artifact_sink"},
              {"max_buffers", 2},
              {"wait_on_eos", false}}},
            {"evidence_class", profile.at("evidence_class")}};
}

void preflight_pipeline(const Json& profile) {
    ensure_gstreamer_initialized();
    for (const std::string& factory_name :
         {profile.at("source").at("factory").get<std::string>(),
          profile.at("filter").at("factory").get<std::string>(),
          std::string("appsink")}) {
        GstElementFactory* factory =
            gst_element_factory_find(factory_name.c_str());
        if (factory == nullptr) {
            throw ImageProcessError(
                satellite::EXIT_DEPENDENCY,
                "required GStreamer factory not found: " + factory_name);
        }
        gst_object_unref(factory);
    }
}

PipelineResult run_pipeline(const Json&                  profile,
                            const std::filesystem::path& input_path,
                            std::size_t                  max_frames) {
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

        PipelineResult result;
        GstBus*        bus = gst_element_get_bus(pipeline);
        const auto     deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(profile.at("timeout_sec").get<unsigned int>());
        bool done = false;
        while (!done && result.frames.size() < max_frames) {
            GstSample* sample =
                gst_app_sink_try_pull_sample(app_sink, 100 * GST_MSECOND);
            if (sample != nullptr) {
                result.frames.push_back(
                    copy_sample(sample, result.frames.size()));
                gst_sample_unref(sample);
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
            if (std::chrono::steady_clock::now() >= deadline) {
                gst_object_unref(bus);
                throw ImageProcessError(satellite::EXIT_RETRYABLE,
                                        "GStreamer pipeline timed out");
            }
        }
        gst_object_unref(bus);

        result.provenance = {{"gstreamer_version", gst_version_string()},
                             {"source", factory_provenance(source_name)},
                             {"filter", factory_provenance(filter_name)},
                             {"sink", factory_provenance("appsink")},
                             {"filter_role", profile.at("filter").at("role")},
                             {"evidence_class", profile.at("evidence_class")}};
        cleanup();
        return result;
    } catch (...) {
        cleanup();
        throw;
    }
}

}  // namespace image_process
