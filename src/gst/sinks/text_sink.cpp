#include <fcntl.h>
#include <gst/base/gstbasesink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "image_process/gst_meta_v1.h"

namespace {

using Json = nlohmann::json;

struct CategoryMap {
    std::string                     id;
    std::map<uint32_t, std::string> types;
};

struct TextSinkState {
    std::string           work_dir;
    std::string           mode = "targets";
    std::string           sensor_id;
    std::string           acquisition_mode;
    std::string           category_map_path;
    std::string           crop_dir;
    CategoryMap           category_map;
    std::filesystem::path role_partial;
    std::filesystem::path role_final;
    std::filesystem::path meta_partial;
    std::filesystem::path meta_final;
    std::ofstream         role_out;
    std::ofstream         meta_out;
    std::size_t           frame_count   = 0;
    std::int64_t          next_frame_id = 0;
    bool                  started       = false;
    bool                  published     = false;
    bool                  failed        = false;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-text-sink-state");
}

bool load_category_map(const std::filesystem::path& path,
                       CategoryMap&                 out,
                       std::string&                 error) {
    std::ifstream input(path);
    if (!input) {
        error = "category map cannot be opened";
        return false;
    }
    Json document;
    try {
        input >> document;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!document.is_object() || !document.contains("id") ||
        !document.contains("classes") || !document.at("classes").is_array()) {
        error = "category map is missing id or classes";
        return false;
    }
    out.id = document.at("id").get<std::string>();
    out.types.clear();
    for (const auto& item : document.at("classes")) {
        if (!item.contains("index") || !item.contains("type")) {
            error = "category map class is missing index or type";
            return false;
        }
        out.types[item.at("index").get<uint32_t>()] =
            item.at("type").get<std::string>();
    }
    return true;
}

bool inverse_bbox(const IpBBoxV1&         filter_bbox,
                  const IpGeometryMetaV1& geometry,
                  Json&                   original_bbox,
                  double&                 center_x,
                  double&                 center_y,
                  std::string&            error) {
    const auto map_back = [&](double x, double y, double& ox, double& oy) {
        ox = (x - geometry.map.offset_x) / geometry.map.scale_x;
        oy = (y - geometry.map.offset_y) / geometry.map.scale_y;
    };
    double x0 = 0;
    double y0 = 0;
    double x1 = 0;
    double y1 = 0;
    double x2 = 0;
    double y2 = 0;
    double x3 = 0;
    double y3 = 0;
    map_back(filter_bbox.x_min, filter_bbox.y_min, x0, y0);
    map_back(filter_bbox.x_max, filter_bbox.y_min, x1, y1);
    map_back(filter_bbox.x_min, filter_bbox.y_max, x2, y2);
    map_back(filter_bbox.x_max, filter_bbox.y_max, x3, y3);
    const double x_min = std::min(std::min(x0, x1), std::min(x2, x3));
    const double y_min = std::min(std::min(y0, y1), std::min(y2, y3));
    const double x_max = std::max(std::max(x0, x1), std::max(x2, x3));
    const double y_max = std::max(std::max(y0, y1), std::max(y2, y3));
    if (!std::isfinite(x_min) || !std::isfinite(y_min) ||
        !std::isfinite(x_max) || !std::isfinite(y_max) || x_min < 0.0 ||
        y_min < 0.0 || x_max > geometry.original_width ||
        y_max > geometry.original_height || x_min >= x_max || y_min >= y_max) {
        error = "inversed bbox is non-finite or outside the original image";
        return false;
    }
    original_bbox = {
        {"x_min", x_min}, {"y_min", y_min}, {"x_max", x_max}, {"y_max", y_max}};
    center_x = (x_min + x_max) / 2.0;
    center_y = (y_min + y_max) / 2.0;
    return true;
}

bool bbox_in_filter(const IpBBoxV1& bbox, const IpGeometryMetaV1& geometry) {
    return bbox.x_min >= 0.0 && bbox.y_min >= 0.0 &&
           bbox.x_max <= geometry.filter_width &&
           bbox.y_max <= geometry.filter_height && bbox.x_min < bbox.x_max &&
           bbox.y_min < bbox.y_max;
}

void remove_path(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

bool write_gray8_tiff(const std::filesystem::path& path,
                      const std::uint8_t*          src,
                      int                          width,
                      int                          height,
                      int                          src_stride) {
    if (width <= 0 || height <= 0 || src == nullptr) { return false; }
    const std::uint32_t image_bytes =
        static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height);
    constexpr std::uint16_t kEntryCount = 9;
    const std::uint32_t     header_size = 8U;
    const std::uint32_t     ifd_size =
        2U + static_cast<std::uint32_t>(kEntryCount) * 12U + 4U;
    const std::uint32_t       strip_offset = header_size + ifd_size;
    std::vector<std::uint8_t> file(strip_offset + image_bytes, 0);
    file[0]          = 'I';
    file[1]          = 'I';
    file[2]          = 42;
    file[3]          = 0;
    const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        file[offset]     = static_cast<std::uint8_t>(value & 0xFFU);
        file[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    };
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        put16(offset, static_cast<std::uint16_t>(value & 0xFFFFU));
        put16(offset + 2, static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU));
    };
    put32(4, header_size);
    put16(header_size, kEntryCount);
    std::size_t cursor      = header_size + 2U;
    const auto  write_entry = [&](std::uint16_t tag, std::uint16_t type,
                                 std::uint32_t count, std::uint32_t value) {
        put16(cursor, tag);
        put16(cursor + 2, type);
        put32(cursor + 4, count);
        put32(cursor + 8, value);
        cursor += 12U;
    };
    write_entry(256, 3, 1, static_cast<std::uint32_t>(width));
    write_entry(257, 3, 1, static_cast<std::uint32_t>(height));
    write_entry(258, 3, 1, 8);
    write_entry(259, 3, 1, 1);
    write_entry(262, 3, 1, 1);
    write_entry(273, 4, 1, strip_offset);
    write_entry(277, 3, 1, 1);
    write_entry(278, 3, 1, static_cast<std::uint32_t>(height));
    write_entry(279, 4, 1, image_bytes);
    put32(cursor, 0);
    for (int row = 0; row < height; ++row) {
        std::memcpy(
            file.data() + strip_offset +
                static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
            src + static_cast<std::size_t>(row) *
                      static_cast<std::size_t>(src_stride),
            static_cast<std::size_t>(width));
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

std::string sanitize_class_dir(const std::string& type) {
    std::string out;
    out.reserve(type.size());
    for (const char ch : type) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                        ch == '-';
        out.push_back(ok ? ch : '_');
    }
    return out.empty() ? "unknown" : out;
}

struct MappedVideoFrame {
    GstVideoFrame frame{};
    bool          mapped = false;
    ~MappedVideoFrame() {
        if (mapped) { gst_video_frame_unmap(&frame); }
    }
};

bool write_crop_tiff(TextSinkState*       value_state,
                     const GstVideoFrame& frame,
                     const IpBBoxV1&      bbox,
                     const std::string&   class_type,
                     std::uint32_t        frame_id,
                     std::uint32_t        target_index,
                     std::string&         error) {
    const int width  = GST_VIDEO_FRAME_WIDTH(&frame);
    const int height = GST_VIDEO_FRAME_HEIGHT(&frame);
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
    const int x0     = std::max(0, static_cast<int>(std::floor(bbox.x_min)));
    const int y0     = std::max(0, static_cast<int>(std::floor(bbox.y_min)));
    const int x1     = std::min(width, static_cast<int>(std::ceil(bbox.x_max)));
    const int y1 = std::min(height, static_cast<int>(std::ceil(bbox.y_max)));
    if (x1 <= x0 || y1 <= y0) { return true; }
    const std::filesystem::path dir =
        std::filesystem::path(value_state->crop_dir) /
        sanitize_class_dir(class_type);
    std::error_code fs_error;
    std::filesystem::create_directories(dir, fs_error);
    if (fs_error) {
        error = "cannot create crop directory";
        return false;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "f%06u_d%u.tif", frame_id, target_index);
    const auto* plane =
        static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    if (!write_gray8_tiff(dir / name, plane + y0 * stride + x0, x1 - x0,
                          y1 - y0, stride)) {
        error = "failed to write crop TIFF";
        return false;
    }
    return true;
}

bool fsync_and_rename(const std::filesystem::path& partial,
                      const std::filesystem::path& final_path) {
    const int fd = ::open(partial.c_str(), O_RDONLY);
    if (fd < 0) { return false; }
    const int synced = ::fsync(fd);
    ::close(fd);
    if (synced != 0) { return false; }
    std::error_code error;
    std::filesystem::rename(partial, final_path, error);
    return !error;
}

}  // namespace

typedef struct _IpTextSink {
    GstBaseSink parent;
} IpTextSink;

typedef struct _IpTextSinkClass {
    GstBaseSinkClass parent_class;
} IpTextSinkClass;

G_DEFINE_TYPE(IpTextSink, ip_text_sink, GST_TYPE_BASE_SINK)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyWorkDir,
    kPropertyMode,
    kPropertySensorId,
    kPropertyAcquisitionMode,
    kPropertyCategoryMapPath,
    kPropertyCropDir,
};

TextSinkState* state(IpTextSink* sink) {
    return static_cast<TextSinkState*>(
        g_object_get_qdata(G_OBJECT(sink), state_quark()));
}

void fail_closed(IpTextSink* sink, bool dependency, const char* message) {
    state(sink)->failed = true;
    if (dependency) {
        GST_ELEMENT_ERROR(sink, RESOURCE, NOT_FOUND, ("%s", message),
                          (nullptr));
    }
    else {
        GST_ELEMENT_ERROR(sink, STREAM, FORMAT, ("%s", message), (nullptr));
    }
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpTextSink*>(object));
    switch (property_id) {
        case kPropertyWorkDir:
            value_state->work_dir = g_value_get_string(value) == nullptr
                                        ? ""
                                        : g_value_get_string(value);
            break;
        case kPropertyMode:
            value_state->mode = g_value_get_string(value) == nullptr
                                    ? ""
                                    : g_value_get_string(value);
            break;
        case kPropertySensorId:
            value_state->sensor_id = g_value_get_string(value) == nullptr
                                         ? ""
                                         : g_value_get_string(value);
            break;
        case kPropertyAcquisitionMode:
            value_state->acquisition_mode = g_value_get_string(value) == nullptr
                                                ? ""
                                                : g_value_get_string(value);
            break;
        case kPropertyCategoryMapPath:
            value_state->category_map_path =
                g_value_get_string(value) == nullptr
                    ? ""
                    : g_value_get_string(value);
            break;
        case kPropertyCropDir:
            value_state->crop_dir = g_value_get_string(value) == nullptr
                                        ? ""
                                        : g_value_get_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

void get_property(GObject*    object,
                  guint       property_id,
                  GValue*     value,
                  GParamSpec* spec) {
    auto* value_state = state(reinterpret_cast<IpTextSink*>(object));
    switch (property_id) {
        case kPropertyWorkDir:
            g_value_set_string(value, value_state->work_dir.c_str());
            break;
        case kPropertyMode:
            g_value_set_string(value, value_state->mode.c_str());
            break;
        case kPropertySensorId:
            g_value_set_string(value, value_state->sensor_id.c_str());
            break;
        case kPropertyAcquisitionMode:
            g_value_set_string(value, value_state->acquisition_mode.c_str());
            break;
        case kPropertyCategoryMapPath:
            g_value_set_string(value, value_state->category_map_path.c_str());
            break;
        case kPropertyCropDir:
            g_value_set_string(value, value_state->crop_dir.c_str());
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

gboolean start(GstBaseSink* base_sink) {
    auto* sink                 = reinterpret_cast<IpTextSink*>(base_sink);
    auto* value_state          = state(sink);
    value_state->frame_count   = 0;
    value_state->next_frame_id = 0;
    value_state->published     = false;
    value_state->failed        = false;
    if (value_state->work_dir.empty() ||
        (value_state->mode != "targets" && value_state->mode != "tracks") ||
        value_state->sensor_id.empty() ||
        value_state->acquisition_mode.empty() ||
        value_state->category_map_path.empty()) {
        GST_ELEMENT_ERROR(sink, RESOURCE, SETTINGS,
                          ("TextSink is missing installed output properties"),
                          (nullptr));
        return FALSE;
    }
    std::string error;
    if (!load_category_map(value_state->category_map_path,
                           value_state->category_map, error)) {
        GST_ELEMENT_ERROR(sink, RESOURCE, NOT_FOUND, ("%s", error.c_str()),
                          (nullptr));
        return FALSE;
    }
    const std::filesystem::path work(value_state->work_dir);
    std::error_code             fs_error;
    std::filesystem::create_directories(work, fs_error);
    const std::string role_name =
        value_state->mode == "targets" ? "targets.jsonl" : "tracks.jsonl";
    value_state->role_final   = work / role_name;
    value_state->role_partial = work / (role_name + ".partial");
    value_state->meta_final   = work / "image-meta.jsonl";
    value_state->meta_partial = work / "image-meta.jsonl.partial";
    value_state->role_out.open(value_state->role_partial,
                               std::ios::binary | std::ios::trunc);
    value_state->meta_out.open(value_state->meta_partial,
                               std::ios::binary | std::ios::trunc);
    if (!value_state->role_out || !value_state->meta_out) {
        GST_ELEMENT_ERROR(sink, RESOURCE, OPEN_WRITE,
                          ("cannot create product text partials"), (nullptr));
        return FALSE;
    }
    if (!value_state->crop_dir.empty()) {
        std::filesystem::create_directories(value_state->crop_dir, fs_error);
        if (fs_error) {
            GST_ELEMENT_ERROR(sink, RESOURCE, OPEN_WRITE,
                              ("cannot create crop directory"), (nullptr));
            return FALSE;
        }
    }
    value_state->started = true;
    return TRUE;
}

void discard_partials(TextSinkState* value_state) {
    value_state->role_out.close();
    value_state->meta_out.close();
    remove_path(value_state->role_partial);
    remove_path(value_state->meta_partial);
}

gboolean stop(GstBaseSink* base_sink) {
    auto* value_state = state(reinterpret_cast<IpTextSink*>(base_sink));
    if (!value_state->published) { discard_partials(value_state); }
    value_state->started = false;
    return TRUE;
}

gboolean publish(IpTextSink* sink) {
    auto* value_state = state(sink);
    if (value_state->failed) { return FALSE; }
    if (value_state->frame_count == 0U) {
        discard_partials(value_state);
        return TRUE;
    }
    value_state->role_out.flush();
    value_state->meta_out.flush();
    if (!value_state->role_out || !value_state->meta_out) {
        discard_partials(value_state);
        GST_ELEMENT_ERROR(sink, STREAM, FAILED,
                          ("failed to flush product text partials"), (nullptr));
        return FALSE;
    }
    value_state->role_out.close();
    value_state->meta_out.close();
    if (!fsync_and_rename(value_state->role_partial, value_state->role_final) ||
        !fsync_and_rename(value_state->meta_partial, value_state->meta_final)) {
        remove_path(value_state->role_final);
        remove_path(value_state->meta_final);
        discard_partials(value_state);
        GST_ELEMENT_ERROR(sink, STREAM, FAILED,
                          ("failed to publish product text artifacts"),
                          (nullptr));
        return FALSE;
    }
    value_state->published = true;
    return TRUE;
}

gboolean event(GstBaseSink* base_sink, GstEvent* event) {
    auto* sink = reinterpret_cast<IpTextSink*>(base_sink);
    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        if (!publish(sink)) { return FALSE; }
    }
    return GST_BASE_SINK_CLASS(ip_text_sink_parent_class)
        ->event(base_sink, event);
}

GstFlowReturn render(GstBaseSink* base_sink, GstBuffer* buffer) {
    auto*    sink        = reinterpret_cast<IpTextSink*>(base_sink);
    auto*    value_state = state(sink);
    GstPad*  pad         = GST_BASE_SINK_PAD(base_sink);
    GstCaps* caps        = gst_pad_get_current_caps(pad);
    if (caps == nullptr) {
        fail_closed(sink, false, "TextSink has no negotiated caps");
        return GST_FLOW_ERROR;
    }
    GstVideoInfo   info{};
    const gboolean parsed = gst_video_info_from_caps(&info, caps);
    gst_caps_unref(caps);
    if (!parsed) {
        fail_closed(sink, false, "TextSink caps are not raw video");
        return GST_FLOW_ERROR;
    }

    IpGeometryMetaV1 geometry{};
    if (!ip_buffer_get_geometry_meta(buffer, &geometry)) {
        fail_closed(sink, false, "TextSink requires geometry metadata");
        return GST_FLOW_ERROR;
    }
    if (static_cast<int>(geometry.filter_width) !=
            GST_VIDEO_INFO_WIDTH(&info) ||
        static_cast<int>(geometry.filter_height) !=
            GST_VIDEO_INFO_HEIGHT(&info) ||
        geometry.map.scale_x <= 0.0 || geometry.map.scale_y <= 0.0) {
        fail_closed(sink, false,
                    "geometry does not match caps or has non-positive scale");
        return GST_FLOW_ERROR;
    }

    const bool         targets_mode = value_state->mode == "targets";
    IpDetectionFrameV1 detection{};
    IpTrackingFrameV1  tracking{};
    const bool         has_detection =
        ip_buffer_get_detection_frame(buffer, &detection);
    const bool has_tracking = ip_buffer_get_tracking_frame(buffer, &tracking);
    if (has_detection == has_tracking) {
        fail_closed(sink, false,
                    "TextSink requires exactly one of detection or tracking");
        return GST_FLOW_ERROR;
    }
    if (targets_mode != has_detection) {
        fail_closed(sink, false, "TextSink mode does not match role metadata");
        return GST_FLOW_ERROR;
    }

    uint32_t camera_seconds      = 0;
    uint32_t camera_microseconds = 0;
    uint32_t exposure_time_ns    = 0;
    uint8_t  valid               = 0;
    if (targets_mode) {
        IpCdg00MetaV1 cdg00{};
        if (!ip_buffer_get_cdg00_meta(buffer, &cdg00)) {
            fail_closed(sink, false,
                        "targets sink requires CDG00 window_start");
            return GST_FLOW_ERROR;
        }
        valid               = cdg00.window_start.valid;
        camera_seconds      = cdg00.window_start.camera_seconds;
        camera_microseconds = cdg00.window_start.camera_microseconds;
        exposure_time_ns    = cdg00.window_start.exposure_time_ns;
    }
    else {
        IpAreaFrameMetaV1 area{};
        if (!ip_buffer_get_area_frame_meta(buffer, &area)) {
            fail_closed(sink, false,
                        "tracks sink requires AreaFrame camera time");
            return GST_FLOW_ERROR;
        }
        valid               = area.valid;
        camera_seconds      = area.camera_seconds;
        camera_microseconds = area.camera_microseconds;
        exposure_time_ns    = area.exposure_time_ns;
    }
    if (valid == 0U || camera_microseconds > 999999U) {
        fail_closed(sink, false,
                    "source camera clock is missing or out of range");
        return GST_FLOW_ERROR;
    }

    const uint32_t frame_id =
        targets_mode ? detection.frame_id : tracking.frame_id;
    if (static_cast<std::int64_t>(frame_id) != value_state->next_frame_id) {
        fail_closed(sink, false, "frame_id is not strictly increasing from 0");
        return GST_FLOW_ERROR;
    }

    Json               observation = {{"scale", "camera"},
                                      {"seconds", camera_seconds},
                                      {"microseconds", camera_microseconds}};
    Json               exposure = {{"value", exposure_time_ns}, {"unit", "ns"}};
    Json               items    = Json::array();
    std::set<uint64_t> seen_tracks;
    MappedVideoFrame   mapped_frame;
    if (!value_state->crop_dir.empty()) {
        if (!gst_video_frame_map(&mapped_frame.frame, &info, buffer,
                                 GST_MAP_READ)) {
            fail_closed(sink, false, "TextSink cannot map frame for crops");
            return GST_FLOW_ERROR;
        }
        mapped_frame.mapped = true;
    }
    const uint32_t count =
        targets_mode ? detection.target_count : tracking.tracked_target_count;
    for (uint32_t i = 0; i < count; ++i) {
        IpBBoxV1 bbox{};
        uint32_t class_index = 0;
        double   confidence  = 0.0;
        uint64_t track_id    = 0;
        if (targets_mode) {
            IpDetectionTargetV1 target{};
            if (!ip_buffer_get_detection_target(buffer, i, &target)) {
                fail_closed(sink, false, "detection target accessor failed");
                return GST_FLOW_ERROR;
            }
            bbox        = target.bbox;
            class_index = target.class_index;
            confidence  = target.confidence;
        }
        else {
            IpTrackingTargetV1 target{};
            if (!ip_buffer_get_tracking_target(buffer, i, &target)) {
                fail_closed(sink, false, "tracking target accessor failed");
                return GST_FLOW_ERROR;
            }
            if (!seen_tracks.insert(target.track_id).second) {
                fail_closed(sink, false,
                            "duplicate track_id in the same frame");
                return GST_FLOW_ERROR;
            }
            bbox        = target.bbox;
            class_index = target.class_index;
            track_id    = target.track_id;
        }
        if (!bbox_in_filter(bbox, geometry)) {
            fail_closed(sink, false, "filter bbox is outside the filter plane");
            return GST_FLOW_ERROR;
        }
        const auto type = value_state->category_map.types.find(class_index);
        if (type == value_state->category_map.types.end()) {
            fail_closed(sink, false, "unknown class_index");
            return GST_FLOW_ERROR;
        }
        Json        original_bbox;
        double      center_x = 0;
        double      center_y = 0;
        std::string error;
        if (!inverse_bbox(bbox, geometry, original_bbox, center_x, center_y,
                          error)) {
            fail_closed(sink, false, error.c_str());
            return GST_FLOW_ERROR;
        }
        Json item = {{"bbox", original_bbox},
                     {"class",
                      {{"index", class_index},
                       {"type", type->second},
                       {"mapping_id", value_state->category_map.id}}}};
        if (targets_mode) {
            item["target_id"] = "d" + std::to_string(i);
            item["source_id"] = "detection:" + std::to_string(frame_id) + ":" +
                                std::to_string(i);
            item["confidence"]         = confidence;
            item["geolocation_sample"] = {{"line", center_y},
                                          {"pixel", center_x}};
        }
        else {
            item["track_id"]     = track_id;
            item["image_sample"] = {{"row", center_y}, {"column", center_x}};
        }
        if (mapped_frame.mapped) {
            std::string crop_error;
            if (!write_crop_tiff(value_state, mapped_frame.frame, bbox,
                                 type->second, frame_id, i, crop_error)) {
                fail_closed(sink, false, crop_error.c_str());
                return GST_FLOW_ERROR;
            }
        }
        items.push_back(std::move(item));
    }

    Json record = {{"frame_id", frame_id},
                   {"observation_time", observation},
                   {"exposure_duration", exposure}};
    if (targets_mode) { record["targets"] = items; }
    else { record["tracks"] = items; }
    Json image_meta = {{"sensor_id", value_state->sensor_id},
                       {"acquisition_mode", value_state->acquisition_mode},
                       {"frame_id", frame_id},
                       {"original_width", geometry.original_width},
                       {"original_height", geometry.original_height},
                       {"filter_width", geometry.filter_width},
                       {"filter_height", geometry.filter_height},
                       {"geometry_id", geometry.geometry_id},
                       {"observation_time", observation},
                       {"exposure_duration", exposure}};
    if (targets_mode) { image_meta["target_count"] = items.size(); }
    else { image_meta["tracked_target_count"] = items.size(); }
    value_state->role_out << record.dump() << '\n';
    value_state->meta_out << image_meta.dump() << '\n';
    if (!value_state->role_out || !value_state->meta_out) {
        fail_closed(sink, false, "failed while writing product text partials");
        return GST_FLOW_ERROR;
    }
    ++value_state->frame_count;
    ++value_state->next_frame_id;
    return GST_FLOW_OK;
}

}  // namespace

static void ip_text_sink_class_init(IpTextSinkClass* sink_class) {
    auto* object_class         = G_OBJECT_CLASS(sink_class);
    auto* element_class        = GST_ELEMENT_CLASS(sink_class);
    auto* base_sink_class      = GST_BASE_SINK_CLASS(sink_class);
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyWorkDir,
        g_param_spec_string("work-dir", "work dir",
                            "Task work directory injected by the CLI", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyMode,
        g_param_spec_string("mode", "mode", "targets or tracks", "targets",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertySensorId,
        g_param_spec_string("sensor-id", "sensor id", "Request sensor.id", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyAcquisitionMode,
        g_param_spec_string("acquisition-mode", "acquisition mode",
                            "pushbroom or stare", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyCategoryMapPath,
        g_param_spec_string("category-map-path", "category map path",
                            "Installed category map JSON", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyCropDir,
        g_param_spec_string(
            "crop-dir", "crop dir",
            "Optional class-sorted TIFF crop directory; empty disables crops",
            "",
            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                     G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process Text Sink", "Sink/Text",
        "Writes product JSONL from geometry and detector/tracker metadata",
        "image-process maintainers");
    static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string)GRAY8, "
                        "width=(int)[1,MAX], height=(int)[1,MAX]"));
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    base_sink_class->start  = start;
    base_sink_class->stop   = stop;
    base_sink_class->event  = event;
    base_sink_class->render = render;
}

static void ip_text_sink_init(IpTextSink* sink) {
    g_object_set_qdata_full(
        G_OBJECT(sink), state_quark(), new TextSinkState(),
        [](gpointer data) { delete static_cast<TextSinkState*>(data); });
    gst_base_sink_set_sync(GST_BASE_SINK(sink), FALSE);
}
