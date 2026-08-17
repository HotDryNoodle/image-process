#include "image_process/gst_meta_v1.h"

#include <gst/gst.h>

#include <cstring>

namespace {

gboolean alternate_init(GstMeta*, gpointer, GstBuffer*) { return TRUE; }

bool check(bool condition, const char* message) {
    if (!condition) { g_printerr("gst-meta-v1-test: %s\n", message); }
    return condition;
}

IpCdg00MetaV1 make_cdg00() {
    IpCdg00MetaV1 value{};
    value.abi_version              = IP_GST_META_ABI_VERSION_V1;
    value.struct_size              = sizeof(value);
    value.window_start.struct_size = sizeof(value.window_start);
    value.window_end.struct_size   = sizeof(value.window_end);
    value.window_start.valid       = 1;
    value.window_start.gps_week    = 2300;
    value.window_start.gps_seconds = 42;
    value.window_start.lla[0]      = 120.5F;
    return value;
}

IpAreaFrameMetaV1 make_area_frame() {
    IpAreaFrameMetaV1 value{};
    value.abi_version         = IP_GST_META_ABI_VERSION_V1;
    value.struct_size         = sizeof(value);
    value.valid               = 1;
    value.camera_seconds      = 1;
    value.camera_microseconds = 234567;
    value.exposure_time_ns    = 12345;
    return value;
}

IpGeometryMetaV1 make_geometry() {
    IpGeometryMetaV1 value{};
    value.abi_version     = IP_GST_META_ABI_VERSION_V1;
    value.struct_size     = sizeof(value);
    value.original_width  = 4096;
    value.original_height = 4096;
    value.filter_width    = 4096;
    value.filter_height   = 4096;
    std::memcpy(value.geometry_id, "identity.v1", sizeof("identity.v1"));
    value.map.struct_size = sizeof(value.map);
    value.map.scale_x     = 1.0;
    value.map.scale_y     = 1.0;
    return value;
}

IpBBoxV1 make_bbox(double x_min, double y_min, double x_max, double y_max) {
    IpBBoxV1 bbox{};
    bbox.struct_size = sizeof(bbox);
    bbox.x_min       = x_min;
    bbox.y_min       = y_min;
    bbox.x_max       = x_max;
    bbox.y_max       = y_max;
    return bbox;
}

IpDetectionTargetV1 make_detection_target() {
    IpDetectionTargetV1 target{};
    target.struct_size = sizeof(target);
    target.class_index = 1;
    target.confidence  = 0.9;
    target.bbox        = make_bbox(100.0, 40.0, 140.0, 80.0);
    return target;
}

IpTrackingTargetV1 make_tracking_target() {
    IpTrackingTargetV1 target{};
    target.struct_size = sizeof(target);
    target.track_id    = 7;
    target.class_index = 0;
    target.bbox        = make_bbox(20.0, 30.0, 60.0, 90.0);
    return target;
}

}  // namespace

int main(int argc, char** argv) {
    if (!gst_init_check(&argc, &argv, nullptr)) { return 1; }

    GstBuffer* buffer = gst_buffer_new();
    auto       input  = make_cdg00();
    if (!check(ip_buffer_add_cdg00_meta(buffer, &input), "valid add failed")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    IpCdg00MetaV1 output{};
    if (!check(ip_buffer_get_cdg00_meta(buffer, &output), "valid get failed") ||
        !check(std::memcmp(&input, &output, sizeof(input)) == 0,
               "round trip changed value")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer = gst_buffer_new();
    input.struct_size -= 1U;
    if (!check(!ip_buffer_add_cdg00_meta(buffer, &input),
               "struct-size mismatch was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                       = gst_buffer_new();
    const GstMetaInfo* alternate = gst_meta_register(
        ip_cdg00_meta_api_get_type(), "AlternateCdg00MetaForContractTest",
        sizeof(GstMeta), alternate_init, nullptr, nullptr);
    if (!check(gst_buffer_add_meta(buffer, alternate, nullptr) != nullptr,
               "alternate meta setup failed") ||
        !check(!ip_buffer_get_cdg00_meta(buffer, &output),
               "foreign implementation with same API was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    if (!check(sizeof(IpAreaFrameMetaV1) == 28U, "AreaFrame sizeof") ||
        !check(sizeof(IpGeometryMetaV1) == 128U, "geometry sizeof") ||
        !check(sizeof(IpDetectionFrameV1) == 16U, "detection frame sizeof") ||
        !check(sizeof(IpTrackingTargetV1) == 64U, "tracking target sizeof")) {
        return 1;
    }

    buffer             = gst_buffer_new();
    auto area          = make_area_frame();
    auto area_mismatch = area;
    if (!check(ip_buffer_add_area_frame_meta(buffer, &area),
               "area-frame add failed") ||
        !check(!ip_buffer_add_area_frame_meta(buffer, &area),
               "duplicate area-frame was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    IpAreaFrameMetaV1 area_out{};
    if (!check(ip_buffer_get_area_frame_meta(buffer, &area_out),
               "area-frame get failed") ||
        !check(std::memcmp(&area, &area_out, sizeof(area)) == 0,
               "area-frame round trip changed value")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);
    buffer = gst_buffer_new();
    area_mismatch.struct_size -= 1U;
    if (!check(!ip_buffer_add_area_frame_meta(buffer, &area_mismatch),
               "area-frame struct-size mismatch was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                = gst_buffer_new();
    auto geometry         = make_geometry();
    auto bad_scale        = geometry;
    bad_scale.map.scale_x = 0.0;
    if (!check(!ip_buffer_add_geometry_meta(buffer, &bad_scale),
               "non-positive scale was accepted") ||
        !check(ip_buffer_add_geometry_meta(buffer, &geometry),
               "geometry add failed") ||
        !check(!ip_buffer_add_geometry_meta(buffer, &geometry),
               "duplicate geometry was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                 = gst_buffer_new();
    auto det_frame         = IpDetectionFrameV1{};
    det_frame.abi_version  = IP_GST_META_ABI_VERSION_V1;
    det_frame.struct_size  = sizeof(det_frame);
    det_frame.frame_id     = 0;
    det_frame.target_count = 0;
    if (!check(ip_buffer_add_detection_meta(buffer, &det_frame, nullptr),
               "zero-target detection add failed")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    IpDetectionFrameV1 det_out{};
    if (!check(ip_buffer_get_detection_frame(buffer, &det_out),
               "zero-target detection get failed") ||
        !check(det_out.target_count == 0U, "zero-target count changed") ||
        !check(!ip_buffer_get_detection_target(buffer, 0, nullptr) &&
                   !ip_buffer_add_detection_meta(buffer, &det_frame, nullptr),
               "duplicate detection or invalid target index was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                 = gst_buffer_new();
    auto target            = make_detection_target();
    det_frame.target_count = 1;
    if (!check(ip_buffer_add_detection_meta(buffer, &det_frame, &target),
               "detection target add failed")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    IpDetectionTargetV1 target_out{};
    if (!check(ip_buffer_get_detection_target(buffer, 0, &target_out),
               "detection target get failed") ||
        !check(std::memcmp(&target, &target_out, sizeof(target)) == 0,
               "detection target round trip changed value")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                 = gst_buffer_new();
    det_frame.target_count = IP_GST_META_MAX_TARGETS_PER_FRAME_V1 + 1U;
    if (!check(!ip_buffer_add_detection_meta(buffer, &det_frame, &target),
               "257 detection targets were accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    auto bad_bbox          = target;
    bad_bbox.bbox.x_min    = bad_bbox.bbox.x_max;
    det_frame.target_count = 1;
    if (!check(!ip_buffer_add_detection_meta(buffer, &det_frame, &bad_bbox),
               "degenerate bbox was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                           = gst_buffer_new();
    auto track_frame                 = IpTrackingFrameV1{};
    track_frame.abi_version          = IP_GST_META_ABI_VERSION_V1;
    track_frame.struct_size          = sizeof(track_frame);
    auto track                       = make_tracking_target();
    track_frame.tracked_target_count = 1;
    if (!check(ip_buffer_add_tracking_meta(buffer, &track_frame, &track),
               "tracking add failed")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    IpTrackingTargetV1 track_out{};
    if (!check(ip_buffer_get_tracking_target(buffer, 0, &track_out),
               "tracking get failed") ||
        !check(track_out.track_id == 7U, "track_id changed")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                          = gst_buffer_new();
    const GstMetaInfo* foreign_area = gst_meta_register(
        ip_area_frame_meta_api_get_type(), "AlternateAreaFrameMetaForTest",
        sizeof(GstMeta), alternate_init, nullptr, nullptr);
    IpAreaFrameMetaV1 foreign_out{};
    if (!check(gst_buffer_add_meta(buffer, foreign_area, nullptr) != nullptr,
               "foreign area-frame setup failed") ||
        !check(!ip_buffer_get_area_frame_meta(buffer, &foreign_out),
               "foreign area-frame impl was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);
    return 0;
}
