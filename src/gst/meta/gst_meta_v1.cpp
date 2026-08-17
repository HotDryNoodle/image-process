#include "image_process/gst_meta_v1.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {

struct Cdg00GstMeta {
    GstMeta       meta;
    IpCdg00MetaV1 value;
};

struct ImageDirGstMeta {
    GstMeta          meta;
    IpImageDirMetaV1 value;
};

struct AreaFrameGstMeta {
    GstMeta           meta;
    IpAreaFrameMetaV1 value;
};

struct GeometryGstMeta {
    GstMeta          meta;
    IpGeometryMetaV1 value;
};

struct DetectionGstMeta {
    GstMeta             meta;
    IpDetectionFrameV1  frame;
    IpDetectionTargetV1 targets[IP_GST_META_MAX_TARGETS_PER_FRAME_V1];
};

struct TrackingGstMeta {
    GstMeta            meta;
    IpTrackingFrameV1  frame;
    IpTrackingTargetV1 targets[IP_GST_META_MAX_TARGETS_PER_FRAME_V1];
};

static_assert(sizeof(IpCdg00SampleV1) == 68U,
              "IpCdg00SampleV1 ABI size changed");
static_assert(sizeof(IpCdg00MetaV1) == 144U, "IpCdg00MetaV1 ABI size changed");
static_assert(sizeof(IpImageDirMetaV1) == 2096U,
              "IpImageDirMetaV1 ABI size changed");
static_assert(sizeof(IpAreaFrameMetaV1) == 28U,
              "IpAreaFrameMetaV1 ABI size changed");
static_assert(sizeof(IpScaleOffset2DV1) == 40U,
              "IpScaleOffset2DV1 ABI size changed");
static_assert(sizeof(IpGeometryMetaV1) == 128U,
              "IpGeometryMetaV1 ABI size changed");
static_assert(sizeof(IpBBoxV1) == 40U, "IpBBoxV1 ABI size changed");
static_assert(sizeof(IpDetectionTargetV1) == 56U,
              "IpDetectionTargetV1 ABI size changed");
static_assert(sizeof(IpDetectionFrameV1) == 16U,
              "IpDetectionFrameV1 ABI size changed");
static_assert(sizeof(IpTrackingTargetV1) == 64U,
              "IpTrackingTargetV1 ABI size changed");
static_assert(sizeof(IpTrackingFrameV1) == 16U,
              "IpTrackingFrameV1 ABI size changed");

bool valid_sample(const IpCdg00SampleV1& sample) {
    return sample.struct_size == sizeof(IpCdg00SampleV1);
}

bool valid_cdg00(const IpCdg00MetaV1& value) {
    return value.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           value.struct_size == sizeof(IpCdg00MetaV1) &&
           valid_sample(value.window_start) && valid_sample(value.window_end);
}

bool valid_image_dir(const IpImageDirMetaV1& value) {
    return value.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           value.struct_size == sizeof(IpImageDirMetaV1);
}

bool finite_number(double value) { return std::isfinite(value); }

bool valid_bbox(const IpBBoxV1& bbox) {
    return bbox.struct_size == sizeof(IpBBoxV1) && bbox.reserved_0 == 0U &&
           finite_number(bbox.x_min) && finite_number(bbox.y_min) &&
           finite_number(bbox.x_max) && finite_number(bbox.y_max) &&
           bbox.x_min < bbox.x_max && bbox.y_min < bbox.y_max;
}

bool valid_area_frame(const IpAreaFrameMetaV1& value) {
    return value.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           value.struct_size == sizeof(IpAreaFrameMetaV1);
}

bool valid_scale_offset(const IpScaleOffset2DV1& map) {
    return map.struct_size == sizeof(IpScaleOffset2DV1) &&
           map.reserved_0 == 0U && finite_number(map.scale_x) &&
           finite_number(map.scale_y) && finite_number(map.offset_x) &&
           finite_number(map.offset_y) && map.scale_x > 0.0 &&
           map.scale_y > 0.0;
}

bool valid_geometry(const IpGeometryMetaV1& value) {
    if (value.abi_version != IP_GST_META_ABI_VERSION_V1 ||
        value.struct_size != sizeof(IpGeometryMetaV1) ||
        value.original_width < 1U || value.original_height < 1U ||
        value.filter_width < 1U || value.filter_height < 1U ||
        !valid_scale_offset(value.map) || value.geometry_id[0] == '\0') {
        return false;
    }
    return std::memchr(value.geometry_id, '\0', sizeof(value.geometry_id)) !=
           nullptr;
}

bool valid_detection_target(const IpDetectionTargetV1& target) {
    return target.struct_size == sizeof(IpDetectionTargetV1) &&
           finite_number(target.confidence) && target.confidence >= 0.0 &&
           target.confidence <= 1.0 && valid_bbox(target.bbox);
}

bool valid_detection_frame(const IpDetectionFrameV1& frame) {
    return frame.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           frame.struct_size == sizeof(IpDetectionFrameV1) &&
           frame.target_count <= IP_GST_META_MAX_TARGETS_PER_FRAME_V1;
}

bool valid_tracking_target(const IpTrackingTargetV1& target) {
    return target.struct_size == sizeof(IpTrackingTargetV1) &&
           target.reserved_0 == 0U && target.reserved_1 == 0U &&
           valid_bbox(target.bbox);
}

bool valid_tracking_frame(const IpTrackingFrameV1& frame) {
    return frame.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           frame.struct_size == sizeof(IpTrackingFrameV1) &&
           frame.tracked_target_count <= IP_GST_META_MAX_TARGETS_PER_FRAME_V1;
}

bool has_meta_info(const GstBuffer* buffer, const GstMetaInfo* info) {
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info == info) { return true; }
    }
    return false;
}

gboolean cdg00_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<Cdg00GstMeta*>(meta);
    std::memset(&typed->value, 0, sizeof(typed->value));
    return TRUE;
}

gboolean cdg00_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const Cdg00GstMeta*>(meta);
    return ip_buffer_add_cdg00_meta(destination, &typed->value);
}

gboolean image_dir_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<ImageDirGstMeta*>(meta);
    std::memset(&typed->value, 0, sizeof(typed->value));
    return TRUE;
}

gboolean image_dir_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const ImageDirGstMeta*>(meta);
    return ip_buffer_add_image_dir_meta(destination, &typed->value);
}

gboolean area_frame_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<AreaFrameGstMeta*>(meta);
    std::memset(&typed->value, 0, sizeof(typed->value));
    return TRUE;
}

gboolean area_frame_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const AreaFrameGstMeta*>(meta);
    return ip_buffer_add_area_frame_meta(destination, &typed->value);
}

gboolean geometry_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<GeometryGstMeta*>(meta);
    std::memset(&typed->value, 0, sizeof(typed->value));
    return TRUE;
}

gboolean geometry_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const GeometryGstMeta*>(meta);
    return ip_buffer_add_geometry_meta(destination, &typed->value);
}

gboolean detection_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<DetectionGstMeta*>(meta);
    std::memset(&typed->frame, 0, sizeof(typed->frame));
    std::memset(typed->targets, 0, sizeof(typed->targets));
    return TRUE;
}

gboolean detection_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const DetectionGstMeta*>(meta);
    return ip_buffer_add_detection_meta(destination, &typed->frame,
                                        typed->targets);
}

gboolean tracking_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<TrackingGstMeta*>(meta);
    std::memset(&typed->frame, 0, sizeof(typed->frame));
    std::memset(typed->targets, 0, sizeof(typed->targets));
    return TRUE;
}

gboolean tracking_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const TrackingGstMeta*>(meta);
    return ip_buffer_add_tracking_meta(destination, &typed->frame,
                                       typed->targets);
}

}  // namespace

GType ip_cdg00_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "cdg00", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_cdg00_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_cdg00_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_cdg00_meta_api_get_type(), "ImageProcessCdg00MetaV1",
            sizeof(Cdg00GstMeta), cdg00_init, nullptr, cdg00_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_cdg00_meta(GstBuffer*           buffer,
                                  const IpCdg00MetaV1* value) {
    if (buffer == nullptr || value == nullptr || !valid_cdg00(*value)) {
        return FALSE;
    }
    auto* meta = reinterpret_cast<Cdg00GstMeta*>(
        gst_buffer_add_meta(buffer, ip_cdg00_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->value = *value;
    return TRUE;
}

gboolean ip_buffer_get_cdg00_meta(const GstBuffer* buffer,
                                  IpCdg00MetaV1*   value) {
    if (buffer == nullptr || value == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_cdg00_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const Cdg00GstMeta*>(current);
        if (!valid_cdg00(meta->value)) { return FALSE; }
        *value = meta->value;
        return TRUE;
    }
    return FALSE;
}

GType ip_image_dir_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "file", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_image_dir_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_image_dir_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_image_dir_meta_api_get_type(), "ImageProcessImageDirMetaV1",
            sizeof(ImageDirGstMeta), image_dir_init, nullptr,
            image_dir_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_image_dir_meta(GstBuffer*              buffer,
                                      const IpImageDirMetaV1* value) {
    if (buffer == nullptr || value == nullptr || !valid_image_dir(*value)) {
        return FALSE;
    }
    auto* meta = reinterpret_cast<ImageDirGstMeta*>(
        gst_buffer_add_meta(buffer, ip_image_dir_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->value = *value;
    return TRUE;
}

gboolean ip_buffer_get_image_dir_meta(const GstBuffer*  buffer,
                                      IpImageDirMetaV1* value) {
    if (buffer == nullptr || value == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_image_dir_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const ImageDirGstMeta*>(current);
        if (!valid_image_dir(meta->value)) { return FALSE; }
        *value = meta->value;
        return TRUE;
    }
    return FALSE;
}

GType ip_area_frame_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "area-frame", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_area_frame_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_area_frame_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_area_frame_meta_api_get_type(), "ImageProcessAreaFrameMetaV1",
            sizeof(AreaFrameGstMeta), area_frame_init, nullptr,
            area_frame_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_area_frame_meta(GstBuffer*               buffer,
                                       const IpAreaFrameMetaV1* value) {
    if (buffer == nullptr || value == nullptr || !valid_area_frame(*value) ||
        has_meta_info(buffer, ip_area_frame_meta_get_info())) {
        return FALSE;
    }
    auto* meta = reinterpret_cast<AreaFrameGstMeta*>(
        gst_buffer_add_meta(buffer, ip_area_frame_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->value = *value;
    return TRUE;
}

gboolean ip_buffer_get_area_frame_meta(const GstBuffer*   buffer,
                                       IpAreaFrameMetaV1* value) {
    if (buffer == nullptr || value == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_area_frame_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const AreaFrameGstMeta*>(current);
        if (!valid_area_frame(meta->value)) { return FALSE; }
        *value = meta->value;
        return TRUE;
    }
    return FALSE;
}

GType ip_geometry_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "geometry", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_geometry_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_geometry_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_geometry_meta_api_get_type(), "ImageProcessGeometryMetaV1",
            sizeof(GeometryGstMeta), geometry_init, nullptr,
            geometry_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_geometry_meta(GstBuffer*              buffer,
                                     const IpGeometryMetaV1* value) {
    if (buffer == nullptr || value == nullptr || !valid_geometry(*value) ||
        has_meta_info(buffer, ip_geometry_meta_get_info())) {
        return FALSE;
    }
    auto* meta = reinterpret_cast<GeometryGstMeta*>(
        gst_buffer_add_meta(buffer, ip_geometry_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->value = *value;
    return TRUE;
}

gboolean ip_buffer_get_geometry_meta(const GstBuffer*  buffer,
                                     IpGeometryMetaV1* value) {
    if (buffer == nullptr || value == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_geometry_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const GeometryGstMeta*>(current);
        if (!valid_geometry(meta->value)) { return FALSE; }
        *value = meta->value;
        return TRUE;
    }
    return FALSE;
}

GType ip_detection_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "detection", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_detection_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_detection_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_detection_meta_api_get_type(), "ImageProcessDetectionMetaV1",
            sizeof(DetectionGstMeta), detection_init, nullptr,
            detection_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_detection_meta(GstBuffer*                 buffer,
                                      const IpDetectionFrameV1*  frame,
                                      const IpDetectionTargetV1* targets) {
    if (buffer == nullptr || frame == nullptr ||
        !valid_detection_frame(*frame) ||
        has_meta_info(buffer, ip_detection_meta_get_info())) {
        return FALSE;
    }
    if (frame->target_count > 0U && targets == nullptr) { return FALSE; }
    for (uint32_t i = 0; i < frame->target_count; ++i) {
        if (!valid_detection_target(targets[i])) { return FALSE; }
    }
    auto* meta = reinterpret_cast<DetectionGstMeta*>(
        gst_buffer_add_meta(buffer, ip_detection_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->frame = *frame;
    if (frame->target_count > 0U) {
        std::memcpy(meta->targets, targets,
                    sizeof(IpDetectionTargetV1) * frame->target_count);
    }
    return TRUE;
}

gboolean ip_buffer_get_detection_frame(const GstBuffer*    buffer,
                                       IpDetectionFrameV1* frame) {
    if (buffer == nullptr || frame == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_detection_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const DetectionGstMeta*>(current);
        if (!valid_detection_frame(meta->frame)) { return FALSE; }
        *frame = meta->frame;
        return TRUE;
    }
    return FALSE;
}

gboolean ip_buffer_get_detection_target(const GstBuffer*     buffer,
                                        uint32_t             index,
                                        IpDetectionTargetV1* target) {
    if (buffer == nullptr || target == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_detection_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const DetectionGstMeta*>(current);
        if (!valid_detection_frame(meta->frame) ||
            index >= meta->frame.target_count ||
            !valid_detection_target(meta->targets[index])) {
            return FALSE;
        }
        *target = meta->targets[index];
        return TRUE;
    }
    return FALSE;
}

GType ip_tracking_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "tracking", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_tracking_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_tracking_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_tracking_meta_api_get_type(), "ImageProcessTrackingMetaV1",
            sizeof(TrackingGstMeta), tracking_init, nullptr,
            tracking_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_tracking_meta(GstBuffer*                buffer,
                                     const IpTrackingFrameV1*  frame,
                                     const IpTrackingTargetV1* targets) {
    if (buffer == nullptr || frame == nullptr ||
        !valid_tracking_frame(*frame) ||
        has_meta_info(buffer, ip_tracking_meta_get_info())) {
        return FALSE;
    }
    if (frame->tracked_target_count > 0U && targets == nullptr) {
        return FALSE;
    }
    for (uint32_t i = 0; i < frame->tracked_target_count; ++i) {
        if (!valid_tracking_target(targets[i])) { return FALSE; }
    }
    auto* meta = reinterpret_cast<TrackingGstMeta*>(
        gst_buffer_add_meta(buffer, ip_tracking_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->frame = *frame;
    if (frame->tracked_target_count > 0U) {
        std::memcpy(meta->targets, targets,
                    sizeof(IpTrackingTargetV1) * frame->tracked_target_count);
    }
    return TRUE;
}

gboolean ip_buffer_get_tracking_frame(const GstBuffer*   buffer,
                                      IpTrackingFrameV1* frame) {
    if (buffer == nullptr || frame == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_tracking_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const TrackingGstMeta*>(current);
        if (!valid_tracking_frame(meta->frame)) { return FALSE; }
        *frame = meta->frame;
        return TRUE;
    }
    return FALSE;
}

gboolean ip_buffer_get_tracking_target(const GstBuffer*    buffer,
                                       uint32_t            index,
                                       IpTrackingTargetV1* target) {
    if (buffer == nullptr || target == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_tracking_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const TrackingGstMeta*>(current);
        if (!valid_tracking_frame(meta->frame) ||
            index >= meta->frame.tracked_target_count ||
            !valid_tracking_target(meta->targets[index])) {
            return FALSE;
        }
        *target = meta->targets[index];
        return TRUE;
    }
    return FALSE;
}
