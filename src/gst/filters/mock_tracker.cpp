#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

#include "image_process/gst_meta_v1.h"

namespace {

struct MockTrackerState {
    double   x_min        = 20.0;
    double   y_min        = 30.0;
    double   x_max        = 60.0;
    double   y_max        = 90.0;
    gint64   track_id     = 7;
    int      class_index  = 0;
    bool     emit_targets = true;
    uint32_t frame_id     = 0;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-mock-tracker-state");
}

}  // namespace

typedef struct _IpMockTracker {
    GstBaseTransform parent;
} IpMockTracker;

typedef struct _IpMockTrackerClass {
    GstBaseTransformClass parent_class;
} IpMockTrackerClass;

G_DEFINE_TYPE(IpMockTracker, ip_mock_tracker, GST_TYPE_BASE_TRANSFORM)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyXMin,
    kPropertyYMin,
    kPropertyXMax,
    kPropertyYMax,
    kPropertyTrackId,
    kPropertyClassIndex,
    kPropertyEmitTargets,
};

MockTrackerState* state(IpMockTracker* element) {
    return static_cast<MockTrackerState*>(
        g_object_get_qdata(G_OBJECT(element), state_quark()));
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpMockTracker*>(object));
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
        case kPropertyTrackId:
            value_state->track_id = g_value_get_int64(value);
            break;
        case kPropertyClassIndex:
            value_state->class_index = g_value_get_int(value);
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
    auto* value_state = state(reinterpret_cast<IpMockTracker*>(object));
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
        case kPropertyTrackId:
            g_value_set_int64(value, value_state->track_id);
            break;
        case kPropertyClassIndex:
            g_value_set_int(value, value_state->class_index);
            break;
        case kPropertyEmitTargets:
            g_value_set_boolean(value, value_state->emit_targets);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

GstFlowReturn transform_ip(GstBaseTransform* trans, GstBuffer* buffer) {
    auto*              element     = reinterpret_cast<IpMockTracker*>(trans);
    auto*              value_state = state(element);
    IpGeometryMetaV1   geometry{};
    IpAreaFrameMetaV1  area{};
    IpDetectionFrameV1 existing_detection{};
    IpTrackingFrameV1  existing_tracking{};
    if (!ip_buffer_get_geometry_meta(buffer, &geometry) ||
        !ip_buffer_get_area_frame_meta(buffer, &area)) {
        GST_ELEMENT_ERROR(
            element, STREAM, FORMAT,
            ("MockTracker requires geometry and AreaFrame metadata"),
            (nullptr));
        return GST_FLOW_ERROR;
    }
    if (ip_buffer_get_detection_frame(buffer, &existing_detection) ||
        ip_buffer_get_tracking_frame(buffer, &existing_tracking)) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("MockTracker input already carries role metadata"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }

    IpTrackingFrameV1 frame{};
    frame.abi_version          = IP_GST_META_ABI_VERSION_V1;
    frame.struct_size          = sizeof(frame);
    frame.frame_id             = value_state->frame_id;
    frame.tracked_target_count = value_state->emit_targets ? 1U : 0U;
    IpTrackingTargetV1 target{};
    target.struct_size      = sizeof(target);
    target.track_id         = static_cast<uint64_t>(value_state->track_id);
    target.class_index      = static_cast<uint32_t>(value_state->class_index);
    target.bbox.struct_size = sizeof(target.bbox);
    target.bbox.x_min       = value_state->x_min;
    target.bbox.y_min       = value_state->y_min;
    target.bbox.x_max       = value_state->x_max;
    target.bbox.y_max       = value_state->y_max;
    if (!ip_buffer_add_tracking_meta(
            buffer, &frame,
            frame.tracked_target_count == 0U ? nullptr : &target)) {
        GST_ELEMENT_ERROR(element, STREAM, FAILED,
                          ("failed to attach tracking metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    ++value_state->frame_id;
    return GST_FLOW_OK;
}

gboolean start(GstBaseTransform* trans) {
    state(reinterpret_cast<IpMockTracker*>(trans))->frame_id = 0;
    return TRUE;
}

}  // namespace

static void ip_mock_tracker_class_init(IpMockTrackerClass* filter_class) {
    auto* object_class         = G_OBJECT_CLASS(filter_class);
    auto* element_class        = GST_ELEMENT_CLASS(filter_class);
    auto* transform_class      = GST_BASE_TRANSFORM_CLASS(filter_class);
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyXMin,
        g_param_spec_double("bbox-x-min", "bbox x_min",
                            "Filter-plane bbox x_min", -1e6, 1e6, 20.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyYMin,
        g_param_spec_double("bbox-y-min", "bbox y_min",
                            "Filter-plane bbox y_min", -1e6, 1e6, 30.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyXMax,
        g_param_spec_double("bbox-x-max", "bbox x_max",
                            "Filter-plane bbox x_max", -1e6, 1e6, 60.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyYMax,
        g_param_spec_double("bbox-y-max", "bbox y_max",
                            "Filter-plane bbox y_max", -1e6, 1e6, 90.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyTrackId,
        g_param_spec_int64("track-id", "track id", "Stable mock track id", 0,
                           G_MAXINT64, 7,
                           static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                    G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyClassIndex,
        g_param_spec_int("class-index", "class index", "Mock class index", 0,
                         255, 0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyEmitTargets,
        g_param_spec_boolean("emit-targets", "emit targets",
                             "When false, write a zero-target tracking frame",
                             TRUE,
                             static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process Mock Tracker", "Filter/Video",
        "Deterministic host mock tracker for IP2-M4 contracts",
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
    transform_class->transform_ip = transform_ip;
}

static void ip_mock_tracker_init(IpMockTracker* element) {
    g_object_set_qdata_full(
        G_OBJECT(element), state_quark(), new MockTrackerState(),
        [](gpointer data) { delete static_cast<MockTrackerState*>(data); });
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(element), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(element), FALSE);
}
