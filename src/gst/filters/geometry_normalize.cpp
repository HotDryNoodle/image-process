#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <cmath>
#include <cstring>
#include <string>

#include "image_process/gst_meta_v1.h"

namespace {

struct GeometryState {
    std::string geometry_id     = "identity.v1";
    double      scale_x         = 1.0;
    double      scale_y         = 1.0;
    double      offset_x        = 0.0;
    double      offset_y        = 0.0;
    int         filter_width    = 0;
    int         filter_height   = 0;
    int         original_width  = 0;
    int         original_height = 0;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-geometry-normalize-state");
}

bool valid_geometry_id(const std::string& value) {
    if (value.empty() || value.size() >= IP_GST_META_GEOMETRY_ID_MAX_V1) {
        return false;
    }
    for (const char ch : value) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                        ch == '-';
        if (!ok) { return false; }
    }
    return true;
}

}  // namespace

typedef struct _IpGeometryNormalize {
    GstBaseTransform parent;
} IpGeometryNormalize;

typedef struct _IpGeometryNormalizeClass {
    GstBaseTransformClass parent_class;
} IpGeometryNormalizeClass;

G_DEFINE_TYPE(IpGeometryNormalize,
              ip_geometry_normalize,
              GST_TYPE_BASE_TRANSFORM)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyGeometryId,
    kPropertyScaleX,
    kPropertyScaleY,
    kPropertyOffsetX,
    kPropertyOffsetY,
    kPropertyFilterWidth,
    kPropertyFilterHeight,
};

GeometryState* state(IpGeometryNormalize* element) {
    return static_cast<GeometryState*>(
        g_object_get_qdata(G_OBJECT(element), state_quark()));
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpGeometryNormalize*>(object));
    switch (property_id) {
        case kPropertyGeometryId:
            value_state->geometry_id = g_value_get_string(value) == nullptr
                                           ? ""
                                           : g_value_get_string(value);
            break;
        case kPropertyScaleX:
            value_state->scale_x = g_value_get_double(value);
            break;
        case kPropertyScaleY:
            value_state->scale_y = g_value_get_double(value);
            break;
        case kPropertyOffsetX:
            value_state->offset_x = g_value_get_double(value);
            break;
        case kPropertyOffsetY:
            value_state->offset_y = g_value_get_double(value);
            break;
        case kPropertyFilterWidth:
            value_state->filter_width = g_value_get_int(value);
            break;
        case kPropertyFilterHeight:
            value_state->filter_height = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

void get_property(GObject*    object,
                  guint       property_id,
                  GValue*     value,
                  GParamSpec* spec) {
    auto* value_state = state(reinterpret_cast<IpGeometryNormalize*>(object));
    switch (property_id) {
        case kPropertyGeometryId:
            g_value_set_string(value, value_state->geometry_id.c_str());
            break;
        case kPropertyScaleX:
            g_value_set_double(value, value_state->scale_x);
            break;
        case kPropertyScaleY:
            g_value_set_double(value, value_state->scale_y);
            break;
        case kPropertyOffsetX:
            g_value_set_double(value, value_state->offset_x);
            break;
        case kPropertyOffsetY:
            g_value_set_double(value, value_state->offset_y);
            break;
        case kPropertyFilterWidth:
            g_value_set_int(value, value_state->filter_width);
            break;
        case kPropertyFilterHeight:
            g_value_set_int(value, value_state->filter_height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

int output_width(const GeometryState& value_state, int original_width) {
    return value_state.filter_width > 0 ? value_state.filter_width
                                        : original_width;
}

int output_height(const GeometryState& value_state, int original_height) {
    return value_state.filter_height > 0 ? value_state.filter_height
                                         : original_height;
}

GstCaps* transform_caps(GstBaseTransform* trans,
                        GstPadDirection   direction,
                        GstCaps*          caps,
                        GstCaps*          filter) {
    auto*    value_state = state(reinterpret_cast<IpGeometryNormalize*>(trans));
    GstCaps* result      = gst_caps_copy(caps);
    const guint size     = gst_caps_get_size(result);
    for (guint i = 0; i < size; ++i) {
        GstStructure* structure = gst_caps_get_structure(result, i);
        gint          width     = 0;
        gint          height    = 0;
        if (!gst_structure_get_int(structure, "width", &width) ||
            !gst_structure_get_int(structure, "height", &height)) {
            continue;
        }
        if (direction == GST_PAD_SINK) {
            gst_structure_set(structure, "width", G_TYPE_INT,
                              output_width(*value_state, width), "height",
                              G_TYPE_INT, output_height(*value_state, height),
                              nullptr);
        }
        else {
            gst_structure_set(
                structure, "width", G_TYPE_INT,
                value_state->original_width > 0 ? value_state->original_width
                                                : width,
                "height", G_TYPE_INT,
                value_state->original_height > 0 ? value_state->original_height
                                                 : height,
                nullptr);
        }
    }
    if (filter != nullptr) {
        GstCaps* intersection =
            gst_caps_intersect_full(filter, result, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        return intersection;
    }
    return result;
}

gboolean set_caps(GstBaseTransform* trans, GstCaps* incaps, GstCaps* outcaps) {
    auto*        element     = reinterpret_cast<IpGeometryNormalize*>(trans);
    auto*        value_state = state(element);
    GstVideoInfo in_info{};
    GstVideoInfo out_info{};
    if (!gst_video_info_from_caps(&in_info, incaps) ||
        !gst_video_info_from_caps(&out_info, outcaps)) {
        return FALSE;
    }
    if (GST_VIDEO_INFO_FORMAT(&in_info) != GST_VIDEO_FORMAT_GRAY8 ||
        GST_VIDEO_INFO_FORMAT(&out_info) != GST_VIDEO_FORMAT_GRAY8) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("ImageProcessGeometryNormalize requires GRAY8"),
                          (nullptr));
        return FALSE;
    }
    if (!std::isfinite(value_state->scale_x) ||
        !std::isfinite(value_state->scale_y) || value_state->scale_x <= 0.0 ||
        value_state->scale_y <= 0.0 || !std::isfinite(value_state->offset_x) ||
        !std::isfinite(value_state->offset_y) ||
        !valid_geometry_id(value_state->geometry_id)) {
        GST_ELEMENT_ERROR(element, RESOURCE, SETTINGS,
                          ("invalid geometry scale/offset/id"), (nullptr));
        return FALSE;
    }
    value_state->original_width  = GST_VIDEO_INFO_WIDTH(&in_info);
    value_state->original_height = GST_VIDEO_INFO_HEIGHT(&in_info);
    const int expected_width =
        output_width(*value_state, value_state->original_width);
    const int expected_height =
        output_height(*value_state, value_state->original_height);
    if (GST_VIDEO_INFO_WIDTH(&out_info) != expected_width ||
        GST_VIDEO_INFO_HEIGHT(&out_info) != expected_height) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("geometry output caps do not match filter size"),
                          (nullptr));
        return FALSE;
    }
    gst_base_transform_set_in_place(
        trans, expected_width == value_state->original_width &&
                   expected_height == value_state->original_height &&
                   value_state->scale_x == 1.0 && value_state->scale_y == 1.0 &&
                   value_state->offset_x == 0.0 &&
                   value_state->offset_y == 0.0);
    return TRUE;
}

gboolean attach_geometry(IpGeometryNormalize* element, GstBuffer* buffer) {
    auto*            value_state = state(element);
    IpGeometryMetaV1 meta{};
    meta.abi_version     = IP_GST_META_ABI_VERSION_V1;
    meta.struct_size     = sizeof(meta);
    meta.original_width  = static_cast<uint32_t>(value_state->original_width);
    meta.original_height = static_cast<uint32_t>(value_state->original_height);
    meta.filter_width    = static_cast<uint32_t>(
        output_width(*value_state, value_state->original_width));
    meta.filter_height = static_cast<uint32_t>(
        output_height(*value_state, value_state->original_height));
    std::strncpy(meta.geometry_id, value_state->geometry_id.c_str(),
                 sizeof(meta.geometry_id) - 1U);
    meta.map.struct_size = sizeof(meta.map);
    meta.map.scale_x     = value_state->scale_x;
    meta.map.scale_y     = value_state->scale_y;
    meta.map.offset_x    = value_state->offset_x;
    meta.map.offset_y    = value_state->offset_y;
    if (!ip_buffer_add_geometry_meta(buffer, &meta)) {
        GST_ELEMENT_ERROR(element, STREAM, FAILED,
                          ("failed to attach geometry metadata"), (nullptr));
        return FALSE;
    }
    return TRUE;
}

GstFlowReturn transform_ip(GstBaseTransform* trans, GstBuffer* buffer) {
    auto*            element = reinterpret_cast<IpGeometryNormalize*>(trans);
    IpGeometryMetaV1 existing{};
    if (ip_buffer_get_geometry_meta(buffer, &existing)) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT,
                          ("input already carries geometry metadata"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }
    return attach_geometry(element, buffer) ? GST_FLOW_OK : GST_FLOW_ERROR;
}

GstFlowReturn transform(GstBaseTransform* trans,
                        GstBuffer*        inbuf,
                        GstBuffer*        outbuf) {
    auto*      element     = reinterpret_cast<IpGeometryNormalize*>(trans);
    auto*      value_state = state(element);
    GstMapInfo in_map{};
    GstMapInfo out_map{};
    if (!gst_buffer_map(inbuf, &in_map, GST_MAP_READ) ||
        !gst_buffer_map(outbuf, &out_map, GST_MAP_WRITE)) {
        if (in_map.memory != nullptr) { gst_buffer_unmap(inbuf, &in_map); }
        return GST_FLOW_ERROR;
    }
    std::memset(out_map.data, 0, out_map.size);
    const int in_w  = value_state->original_width;
    const int in_h  = value_state->original_height;
    const int out_w = output_width(*value_state, in_w);
    const int out_h = output_height(*value_state, in_h);
    for (int y = 0; y < out_h; ++y) {
        for (int x = 0; x < out_w; ++x) {
            const double src_x =
                (static_cast<double>(x) - value_state->offset_x) /
                value_state->scale_x;
            const double src_y =
                (static_cast<double>(y) - value_state->offset_y) /
                value_state->scale_y;
            if (src_x < 0.0 || src_y < 0.0 || src_x >= in_w || src_y >= in_h) {
                continue;
            }
            const int ix = static_cast<int>(src_x);
            const int iy = static_cast<int>(src_y);
            out_map.data[static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(out_w) +
                         static_cast<std::size_t>(x)] =
                in_map.data[static_cast<std::size_t>(iy) *
                                static_cast<std::size_t>(in_w) +
                            static_cast<std::size_t>(ix)];
        }
    }
    gst_buffer_unmap(inbuf, &in_map);
    gst_buffer_unmap(outbuf, &out_map);
    if (!gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_METADATA, 0,
                              static_cast<gssize>(-1))) {
        GST_ELEMENT_ERROR(element, STREAM, FAILED,
                          ("failed to copy source metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    return attach_geometry(element, outbuf) ? GST_FLOW_OK : GST_FLOW_ERROR;
}

}  // namespace

static void ip_geometry_normalize_class_init(
    IpGeometryNormalizeClass* filter_class) {
    auto* object_class    = G_OBJECT_CLASS(filter_class);
    auto* element_class   = GST_ELEMENT_CLASS(filter_class);
    auto* transform_class = GST_BASE_TRANSFORM_CLASS(filter_class);

    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyGeometryId,
        g_param_spec_string("geometry-id", "Geometry ID",
                            "Installed geometry identifier", "identity.v1",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyScaleX,
        g_param_spec_double("scale-x", "Scale X", "original→filter scale_x",
                            1e-12, 1e6, 1.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyScaleY,
        g_param_spec_double("scale-y", "Scale Y", "original→filter scale_y",
                            1e-12, 1e6, 1.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyOffsetX,
        g_param_spec_double("offset-x", "Offset X", "original→filter offset_x",
                            -1e6, 1e6, 0.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyOffsetY,
        g_param_spec_double("offset-y", "Offset Y", "original→filter offset_y",
                            -1e6, 1e6, 0.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyFilterWidth,
        g_param_spec_int("filter-width", "Filter width",
                         "Filter-input width; 0 uses original width", 0, 16384,
                         0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyFilterHeight,
        g_param_spec_int("filter-height", "Filter height",
                         "Filter-input height; 0 uses original height", 0,
                         16384, 0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process Geometry Normalize", "Filter/Video",
        "Applies scale+offset geometry and stamps geometry metadata",
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

    transform_class->transform_caps = transform_caps;
    transform_class->set_caps       = set_caps;
    transform_class->transform_ip   = transform_ip;
    transform_class->transform      = transform;
}

static void ip_geometry_normalize_init(IpGeometryNormalize* element) {
    g_object_set_qdata_full(
        G_OBJECT(element), state_quark(), new GeometryState(),
        [](gpointer data) { delete static_cast<GeometryState*>(data); });
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(element), TRUE);
}
