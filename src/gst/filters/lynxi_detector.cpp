#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "image_process/gst_meta_v1.h"
#include "yolox_runtime.hpp"

namespace {

struct LynxiDetectorState {
    image_process::lynxi::YoloxRuntime       runtime;
    image_process::lynxi::YoloxRuntimeConfig config;
    GstVideoInfo                             info{};
    bool                                     have_info     = false;
    std::uint32_t                            next_frame_id = 0;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-lynxi-detector-state");
}

IpBBoxV1 clamp_bbox(const Detection&        detection,
                    const IpGeometryMetaV1& geometry) {
    IpBBoxV1 bbox{};
    bbox.struct_size   = sizeof(bbox);
    const double x_min = std::min(static_cast<double>(detection.bbox.xmin),
                                  static_cast<double>(detection.bbox.xmax));
    const double x_max = std::max(static_cast<double>(detection.bbox.xmin),
                                  static_cast<double>(detection.bbox.xmax));
    const double y_min = std::min(static_cast<double>(detection.bbox.ymin),
                                  static_cast<double>(detection.bbox.ymax));
    const double y_max = std::max(static_cast<double>(detection.bbox.ymin),
                                  static_cast<double>(detection.bbox.ymax));
    bbox.x_min         = std::max(0.0, x_min);
    bbox.y_min         = std::max(0.0, y_min);
    bbox.x_max = std::min(static_cast<double>(geometry.filter_width), x_max);
    bbox.y_max = std::min(static_cast<double>(geometry.filter_height), y_max);
    return bbox;
}

}  // namespace

typedef struct _IpLynxiDetector {
    GstBaseTransform parent;
} IpLynxiDetector;

typedef struct _IpLynxiDetectorClass {
    GstBaseTransformClass parent_class;
} IpLynxiDetectorClass;

G_DEFINE_TYPE(IpLynxiDetector, ip_lynxi_detector, GST_TYPE_BASE_TRANSFORM)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyModelPath,
    kPropertyConfidence,
    kPropertyNms,
    kPropertyYoloType,
    kPropertyDeviceId,
    kPropertyClassNum,
    kPropertyChannelType,
};

LynxiDetectorState* state(IpLynxiDetector* element) {
    return static_cast<LynxiDetectorState*>(
        g_object_get_qdata(G_OBJECT(element), state_quark()));
}

void apply_config(LynxiDetectorState* value_state) {
    value_state->runtime.configure(value_state->config);
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpLynxiDetector*>(object));
    switch (property_id) {
        case kPropertyModelPath:
            value_state->config.model_path =
                g_value_get_string(value) == nullptr
                    ? ""
                    : g_value_get_string(value);
            apply_config(value_state);
            break;
        case kPropertyConfidence:
            value_state->config.score_threshold =
                static_cast<float>(g_value_get_double(value));
            apply_config(value_state);
            break;
        case kPropertyNms:
            value_state->config.nms_threshold =
                static_cast<float>(g_value_get_double(value));
            apply_config(value_state);
            break;
        case kPropertyYoloType:
            value_state->config.yolo_type = g_value_get_int(value);
            apply_config(value_state);
            break;
        case kPropertyDeviceId:
            value_state->config.device_id =
                static_cast<std::uint32_t>(g_value_get_int(value));
            apply_config(value_state);
            break;
        case kPropertyClassNum:
            value_state->config.class_num = g_value_get_int(value);
            apply_config(value_state);
            break;
        case kPropertyChannelType:
            value_state->config.channel_type = g_value_get_int(value);
            apply_config(value_state);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

void get_property(GObject*    object,
                  guint       property_id,
                  GValue*     value,
                  GParamSpec* spec) {
    auto* value_state = state(reinterpret_cast<IpLynxiDetector*>(object));
    switch (property_id) {
        case kPropertyModelPath:
            g_value_set_string(value, value_state->config.model_path.c_str());
            break;
        case kPropertyConfidence:
            g_value_set_double(value, value_state->config.score_threshold);
            break;
        case kPropertyNms:
            g_value_set_double(value, value_state->config.nms_threshold);
            break;
        case kPropertyYoloType:
            g_value_set_int(value, value_state->config.yolo_type);
            break;
        case kPropertyDeviceId:
            g_value_set_int(value,
                            static_cast<int>(value_state->config.device_id));
            break;
        case kPropertyClassNum:
            g_value_set_int(value, value_state->config.class_num);
            break;
        case kPropertyChannelType:
            g_value_set_int(value, value_state->config.channel_type);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

gboolean start(GstBaseTransform* trans) {
    auto* element              = reinterpret_cast<IpLynxiDetector*>(trans);
    auto* value_state          = state(element);
    value_state->next_frame_id = 0;
    apply_config(value_state);
    std::string error;
    if (!value_state->runtime.start(error)) {
        GST_ELEMENT_ERROR(element, RESOURCE, NOT_FOUND, ("%s", error.c_str()),
                          (nullptr));
        return FALSE;
    }
    return TRUE;
}

gboolean stop(GstBaseTransform* trans) {
    state(reinterpret_cast<IpLynxiDetector*>(trans))->runtime.stop();
    return TRUE;
}

gboolean set_caps(GstBaseTransform* trans, GstCaps* in_caps, GstCaps*) {
    auto* value_state = state(reinterpret_cast<IpLynxiDetector*>(trans));
    value_state->have_info =
        gst_video_info_from_caps(&value_state->info, in_caps) != FALSE;
    return value_state->have_info ? TRUE : FALSE;
}

GstFlowReturn transform_ip(GstBaseTransform* trans, GstBuffer* buffer) {
    auto* element     = reinterpret_cast<IpLynxiDetector*>(trans);
    auto* value_state = state(element);
    if (!value_state->runtime.ready() || !value_state->have_info) {
        GST_ELEMENT_ERROR(element, RESOURCE, NOT_FOUND,
                          ("Lynxi detector is not started"), (nullptr));
        return GST_FLOW_ERROR;
    }

    IpGeometryMetaV1   geometry{};
    IpCdg00MetaV1      cdg00{};
    IpDetectionFrameV1 existing_detection{};
    IpTrackingFrameV1  existing_tracking{};
    if (!ip_buffer_get_geometry_meta(buffer, &geometry) ||
        !ip_buffer_get_cdg00_meta(buffer, &cdg00)) {
        GST_ELEMENT_ERROR(
            element, STREAM, FORMAT,
            ("Lynxi detector requires geometry and CDG00 metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    if (ip_buffer_get_detection_frame(buffer, &existing_detection) ||
        ip_buffer_get_tracking_frame(buffer, &existing_tracking)) {
        GST_ELEMENT_ERROR(
            element, STREAM, FORMAT,
            ("Lynxi detector input already carries role metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    if (GST_VIDEO_INFO_FORMAT(&value_state->info) != GST_VIDEO_FORMAT_GRAY8) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("Lynxi detector requires GRAY8"), (nullptr));
        return GST_FLOW_ERROR;
    }

    GstVideoFrame frame{};
    if (!gst_video_frame_map(&frame, &value_state->info, buffer,
                             GST_MAP_READ)) {
        GST_ELEMENT_ERROR(element, STREAM, FAILED,
                          ("failed to map Lynxi detector frame"), (nullptr));
        return GST_FLOW_ERROR;
    }
    std::vector<Detection> detections;
    std::string            error;
    const bool             ok = value_state->runtime.infer_gray8(
        static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
        GST_VIDEO_FRAME_WIDTH(&frame), GST_VIDEO_FRAME_HEIGHT(&frame),
        GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), detections, error);
    gst_video_frame_unmap(&frame);
    if (!ok) {
        GST_ELEMENT_ERROR(element, RESOURCE, FAILED, ("%s", error.c_str()),
                          (nullptr));
        return GST_FLOW_ERROR;
    }

    std::vector<IpDetectionTargetV1> targets;
    targets.reserve(std::min(
        detections.size(),
        static_cast<std::size_t>(IP_GST_META_MAX_TARGETS_PER_FRAME_V1)));
    for (const auto& detection : detections) {
        if (targets.size() >= IP_GST_META_MAX_TARGETS_PER_FRAME_V1) { break; }
        if (detection.id < 0 || detection.score < 0.0F ||
            detection.score > 1.0F) {
            continue;
        }
        IpDetectionTargetV1 target{};
        target.struct_size = sizeof(target);
        target.class_index = static_cast<std::uint32_t>(detection.id);
        target.confidence  = detection.score;
        target.bbox        = clamp_bbox(detection, geometry);
        if (target.bbox.x_min >= target.bbox.x_max ||
            target.bbox.y_min >= target.bbox.y_max) {
            continue;
        }
        targets.push_back(target);
    }

    IpDetectionFrameV1 header{};
    header.abi_version  = IP_GST_META_ABI_VERSION_V1;
    header.struct_size  = sizeof(header);
    header.frame_id     = value_state->next_frame_id++;
    header.target_count = static_cast<std::uint32_t>(targets.size());
    if (!ip_buffer_add_detection_meta(
            buffer, &header,
            header.target_count == 0U ? nullptr : targets.data())) {
        GST_ELEMENT_ERROR(element, STREAM, FAILED,
                          ("failed to attach Lynxi detection metadata"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

}  // namespace

static void ip_lynxi_detector_class_init(IpLynxiDetectorClass* filter_class) {
    auto* object_class         = G_OBJECT_CLASS(filter_class);
    auto* element_class        = GST_ELEMENT_CLASS(filter_class);
    auto* transform_class      = GST_BASE_TRANSFORM_CLASS(filter_class);
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyModelPath,
        g_param_spec_string("model-path", "model path",
                            "Installed Lynxi Net_0 directory", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyConfidence,
        g_param_spec_double("confidence-threshold", "confidence",
                            "Score threshold", 0.0, 1.0, 0.25,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyNms,
        g_param_spec_double("nms-threshold", "nms", "NMS IoU threshold", 0.0,
                            1.0, 0.45,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyYoloType,
        g_param_spec_int("yolo-type", "yolo type",
                         "0=YOLOv5, 1=YOLOv8, 2=YOLOv9", 0, 2, 1,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyDeviceId,
        g_param_spec_int("device-id", "device id",
                         "Lynxi chip id; this stage uses a single chip", 0, 1,
                         0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyClassNum,
        g_param_spec_int("class-num", "class num", "YOLO class count", 1, 80, 2,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyChannelType,
        g_param_spec_int("channel-type", "channel type",
                         "0=gray, 1=rgb, 2=bgr model input", 0, 2, 1,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process Lynxi Detector", "Filter/Video",
        "Single-chip Lynxi YOLO detector for CDG00 product text",
        "image-process maintainers");
    static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string)GRAY8, "
                        "width=(int)[1,MAX], height=(int)[1,MAX]"));
    static GstStaticPadTemplate source_template = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string)GRAY8, "
                        "width=(int)[1,MAX], height=(int)[1,MAX]"));
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &source_template);
    transform_class->start        = start;
    transform_class->stop         = stop;
    transform_class->set_caps     = set_caps;
    transform_class->transform_ip = transform_ip;
}

static void ip_lynxi_detector_init(IpLynxiDetector* element) {
    auto* value_state                = new LynxiDetectorState();
    value_state->config.yolo_type    = 1;
    value_state->config.class_num    = 2;
    value_state->config.channel_type = 1;
    g_object_set_qdata_full(
        G_OBJECT(element), state_quark(), value_state,
        [](gpointer data) { delete static_cast<LynxiDetectorState*>(data); });
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(element), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(element), FALSE);
}
