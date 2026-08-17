#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

#include "image_process/gst_meta_v1.h"

namespace {

struct MockDetectorState {
    double x_min        = 100.0;
    double y_min        = 40.0;
    double x_max        = 140.0;
    double y_max        = 80.0;
    int    class_index  = 1;
    double confidence   = 0.9;
    bool   emit_targets = true;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-mock-detector-state");
}

}  // namespace

typedef struct _IpMockDetector {
    GstBaseTransform parent;
} IpMockDetector;

typedef struct _IpMockDetectorClass {
    GstBaseTransformClass parent_class;
} IpMockDetectorClass;

G_DEFINE_TYPE(IpMockDetector, ip_mock_detector, GST_TYPE_BASE_TRANSFORM)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyXMin,
    kPropertyYMin,
    kPropertyXMax,
    kPropertyYMax,
    kPropertyClassIndex,
    kPropertyConfidence,
    kPropertyEmitTargets,
};

MockDetectorState* state(IpMockDetector* element) {
    return static_cast<MockDetectorState*>(
        g_object_get_qdata(G_OBJECT(element), state_quark()));
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpMockDetector*>(object));
    switch (property_id) {
        case kPropertyXMin:
            value_state->x_min = g_value_get_double(value);
            break;
        case kPropertyYMin:
            value_state->y_min = g_value_get_double(value);
            break;
        case kPropertyXMax:
            value_state->x_max = g_value_get_double(value);
            break;
        case kPropertyYMax:
            value_state->y_max = g_value_get_double(value);
            break;
        case kPropertyClassIndex:
            value_state->class_index = g_value_get_int(value);
            break;
        case kPropertyConfidence:
            value_state->confidence = g_value_get_double(value);
            break;
        case kPropertyEmitTargets:
            value_state->emit_targets = g_value_get_boolean(value) != FALSE;
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

void get_property(GObject*    object,
                  guint       property_id,
                  GValue*     value,
                  GParamSpec* spec) {
    auto* value_state = state(reinterpret_cast<IpMockDetector*>(object));
    switch (property_id) {
        case kPropertyXMin:
            g_value_set_double(value, value_state->x_min);
            break;
        case kPropertyYMin:
            g_value_set_double(value, value_state->y_min);
            break;
        case kPropertyXMax:
            g_value_set_double(value, value_state->x_max);
            break;
        case kPropertyYMax:
            g_value_set_double(value, value_state->y_max);
            break;
        case kPropertyClassIndex:
            g_value_set_int(value, value_state->class_index);
            break;
        case kPropertyConfidence:
            g_value_set_double(value, value_state->confidence);
            break;
        case kPropertyEmitTargets:
            g_value_set_boolean(value, value_state->emit_targets);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

GstFlowReturn transform_ip(GstBaseTransform* trans, GstBuffer* buffer) {
    auto*              element     = reinterpret_cast<IpMockDetector*>(trans);
    auto*              value_state = state(element);
    IpGeometryMetaV1   geometry{};
    IpCdg00MetaV1      cdg00{};
    IpDetectionFrameV1 existing_detection{};
    IpTrackingFrameV1  existing_tracking{};
    if (!ip_buffer_get_geometry_meta(buffer, &geometry) ||
        !ip_buffer_get_cdg00_meta(buffer, &cdg00)) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("MockDetector requires geometry and CDG00 metadata"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }
    if (ip_buffer_get_detection_frame(buffer, &existing_detection) ||
        ip_buffer_get_tracking_frame(buffer, &existing_tracking)) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("MockDetector input already carries role metadata"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }

    IpDetectionFrameV1 frame{};
    frame.abi_version  = IP_GST_META_ABI_VERSION_V1;
    frame.struct_size  = sizeof(frame);
    frame.frame_id     = 0;
    frame.target_count = value_state->emit_targets ? 1U : 0U;
    IpDetectionTargetV1 target{};
    target.struct_size      = sizeof(target);
    target.class_index      = static_cast<uint32_t>(value_state->class_index);
    target.confidence       = value_state->confidence;
    target.bbox.struct_size = sizeof(target.bbox);
    target.bbox.x_min       = value_state->x_min;
    target.bbox.y_min       = value_state->y_min;
    target.bbox.x_max       = value_state->x_max;
    target.bbox.y_max       = value_state->y_max;
    if (!ip_buffer_add_detection_meta(
            buffer, &frame, frame.target_count == 0U ? nullptr : &target)) {
        GST_ELEMENT_ERROR(element, STREAM, FAILED,
                          ("failed to attach detection metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

}  // namespace

static void ip_mock_detector_class_init(IpMockDetectorClass* filter_class) {
    auto* object_class         = G_OBJECT_CLASS(filter_class);
    auto* element_class        = GST_ELEMENT_CLASS(filter_class);
    auto* transform_class      = GST_BASE_TRANSFORM_CLASS(filter_class);
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyXMin,
        g_param_spec_double("bbox-x-min", "bbox x_min",
                            "Filter-plane bbox x_min", -1e6, 1e6, 100.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyYMin,
        g_param_spec_double("bbox-y-min", "bbox y_min",
                            "Filter-plane bbox y_min", -1e6, 1e6, 40.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyXMax,
        g_param_spec_double("bbox-x-max", "bbox x_max",
                            "Filter-plane bbox x_max", -1e6, 1e6, 140.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyYMax,
        g_param_spec_double("bbox-y-max", "bbox y_max",
                            "Filter-plane bbox y_max", -1e6, 1e6, 80.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyClassIndex,
        g_param_spec_int("class-index", "class index", "Mock class index", 0,
                         255, 1,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyConfidence,
        g_param_spec_double("confidence", "confidence", "Mock confidence", 0.0,
                            1.0, 0.9,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyEmitTargets,
        g_param_spec_boolean("emit-targets", "emit targets",
                             "When false, write a zero-target detection frame",
                             TRUE,
                             static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process Mock Detector", "Filter/Video",
        "Deterministic host mock detector for IP2-M4 contracts",
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
    transform_class->transform_ip = transform_ip;
}

static void ip_mock_detector_init(IpMockDetector* element) {
    g_object_set_qdata_full(
        G_OBJECT(element), state_quark(), new MockDetectorState(),
        [](gpointer data) { delete static_cast<MockDetectorState*>(data); });
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(element), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(element), FALSE);
}
