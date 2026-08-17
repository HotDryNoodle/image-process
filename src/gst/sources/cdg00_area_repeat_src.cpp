#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "cdg00_decode.hpp"
#include "image_process/gst_meta_v1.h"

namespace {

using image_process::cdg00::area_frame_from_sample;
using image_process::cdg00::decode_window;
using image_process::cdg00::DecodedWindow;
using image_process::cdg00::kImageWidth;
using image_process::cdg00::kLineSize;

struct AreaRepeatState {
    std::string               location;
    std::vector<std::uint8_t> pixels;
    IpAreaFrameMetaV1         area_frame{};
    std::uint64_t             frame_number = 0;
    int                       image_height = 4096;
    int                       repeat_count = 3;
    double                    fps          = 30.0;
};

GQuark state_quark() {
    return g_quark_from_static_string(
        "image-process-cdg00-area-repeat-source-state");
}

}  // namespace

typedef struct _IpCdg00AreaRepeatSrc {
    GstPushSrc parent;
} IpCdg00AreaRepeatSrc;

typedef struct _IpCdg00AreaRepeatSrcClass {
    GstPushSrcClass parent_class;
} IpCdg00AreaRepeatSrcClass;

G_DEFINE_TYPE(IpCdg00AreaRepeatSrc, ip_cdg00_area_repeat_src, GST_TYPE_PUSH_SRC)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyLocation,
    kPropertyImageHeight,
    kPropertyRepeatCount,
    kPropertyFps,
};

AreaRepeatState* state(IpCdg00AreaRepeatSrc* source) {
    return static_cast<AreaRepeatState*>(
        g_object_get_qdata(G_OBJECT(source), state_quark()));
}

GstCaps* make_caps(const AreaRepeatState& value_state) {
    gint fps_numerator   = 0;
    gint fps_denominator = 1;
    gst_util_double_to_fraction(value_state.fps, &fps_numerator,
                                &fps_denominator);
    return gst_caps_new_simple(
        "video/x-raw", "format", G_TYPE_STRING, "GRAY8", "width", G_TYPE_INT,
        static_cast<int>(kImageWidth), "height", G_TYPE_INT,
        value_state.image_height, "framerate", GST_TYPE_FRACTION, fps_numerator,
        fps_denominator, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
        "interlace-mode", G_TYPE_STRING, "progressive", nullptr);
}

GstCaps* get_caps(GstBaseSrc* base_source, GstCaps* filter) {
    const auto* value_state =
        state(reinterpret_cast<IpCdg00AreaRepeatSrc*>(base_source));
    GstCaps* caps = make_caps(*value_state);
    if (filter == nullptr) { return caps; }
    GstCaps* intersection =
        gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(caps);
    return intersection;
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpCdg00AreaRepeatSrc*>(object));
    switch (property_id) {
        case kPropertyLocation:
            value_state->location = g_value_get_string(value) == nullptr
                                        ? ""
                                        : g_value_get_string(value);
            break;
        case kPropertyImageHeight:
            value_state->image_height = g_value_get_int(value);
            break;
        case kPropertyRepeatCount:
            value_state->repeat_count = g_value_get_int(value);
            break;
        case kPropertyFps:
            value_state->fps = g_value_get_double(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

void get_property(GObject*    object,
                  guint       property_id,
                  GValue*     value,
                  GParamSpec* spec) {
    auto* value_state = state(reinterpret_cast<IpCdg00AreaRepeatSrc*>(object));
    switch (property_id) {
        case kPropertyLocation:
            g_value_set_string(value, value_state->location.c_str());
            break;
        case kPropertyImageHeight:
            g_value_set_int(value, value_state->image_height);
            break;
        case kPropertyRepeatCount:
            g_value_set_int(value, value_state->repeat_count);
            break;
        case kPropertyFps:
            g_value_set_double(value, value_state->fps);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

gboolean start(GstBaseSrc* base_source) {
    auto* source      = reinterpret_cast<IpCdg00AreaRepeatSrc*>(base_source);
    auto* value_state = state(source);
    value_state->pixels.clear();
    value_state->frame_number = 0;
    if (value_state->location.empty()) {
        GST_ELEMENT_ERROR(source, RESOURCE, NOT_FOUND,
                          ("CDG00AreaRepeatSrc location is empty"), (nullptr));
        return FALSE;
    }
    if (value_state->repeat_count < 1) {
        GST_ELEMENT_ERROR(source, RESOURCE, SETTINGS,
                          ("CDG00AreaRepeatSrc repeat-count must be >= 1"),
                          ("repeat-count=%d", value_state->repeat_count));
        return FALSE;
    }

    std::ifstream stream(value_state->location, std::ios::binary);
    if (!stream) {
        GST_ELEMENT_ERROR(source, RESOURCE, OPEN_READ,
                          ("cannot open CDG0.0 input"),
                          ("location=%s", value_state->location.c_str()));
        return FALSE;
    }
    const std::size_t block_size =
        kLineSize * static_cast<std::size_t>(value_state->image_height);
    std::vector<std::uint8_t> raw(block_size);
    stream.read(reinterpret_cast<char*>(raw.data()),
                static_cast<std::streamsize>(raw.size()));
    const std::size_t bytes_read = static_cast<std::size_t>(stream.gcount());
    if (bytes_read < kLineSize) {
        GST_ELEMENT_ERROR(source, STREAM, DECODE,
                          ("CDG0.0 input is shorter than one line"), (nullptr));
        return FALSE;
    }
    raw.resize(bytes_read);

    DecodedWindow decoded;
    if (!decode_window(raw, value_state->image_height, decoded)) {
        GST_ELEMENT_ERROR(source, STREAM, DECODE,
                          ("CDG0.0 first window contains no valid image lines"),
                          (nullptr));
        return FALSE;
    }
    value_state->pixels = std::move(decoded.pixels);
    value_state->area_frame =
        area_frame_from_sample(decoded.metadata.window_start);

    GstCaps*       caps     = make_caps(*value_state);
    const gboolean accepted = gst_base_src_set_caps(base_source, caps);
    gst_caps_unref(caps);
    return accepted;
}

gboolean stop(GstBaseSrc* base_source) {
    auto* value_state =
        state(reinterpret_cast<IpCdg00AreaRepeatSrc*>(base_source));
    value_state->pixels.clear();
    value_state->frame_number = 0;
    return TRUE;
}

GstFlowReturn create(GstPushSrc* push_source, GstBuffer** output_buffer) {
    auto* source      = reinterpret_cast<IpCdg00AreaRepeatSrc*>(push_source);
    auto* value_state = state(source);
    if (value_state->frame_number >=
        static_cast<std::uint64_t>(value_state->repeat_count)) {
        return GST_FLOW_EOS;
    }
    if (value_state->pixels.empty()) {
        GST_ELEMENT_ERROR(source, STREAM, FAILED,
                          ("CDG00AreaRepeatSrc has no decoded window"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }

    GstBuffer* buffer =
        gst_buffer_new_allocate(nullptr, value_state->pixels.size(), nullptr);
    if (buffer == nullptr) { return GST_FLOW_ERROR; }
    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    std::memcpy(map.data, value_state->pixels.data(),
                value_state->pixels.size());
    gst_buffer_unmap(buffer, &map);

    const GstClockTime duration = static_cast<GstClockTime>(
        static_cast<double>(GST_SECOND) / value_state->fps);
    GST_BUFFER_PTS(buffer)        = value_state->frame_number * duration;
    GST_BUFFER_OFFSET(buffer)     = value_state->frame_number;
    GST_BUFFER_OFFSET_END(buffer) = value_state->frame_number + 1U;
    ++value_state->frame_number;
    if (!ip_buffer_add_area_frame_meta(buffer, &value_state->area_frame)) {
        gst_buffer_unref(buffer);
        GST_ELEMENT_ERROR(source, STREAM, FAILED,
                          ("failed to attach AreaFrame metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    *output_buffer = buffer;
    return GST_FLOW_OK;
}

}  // namespace

static void ip_cdg00_area_repeat_src_class_init(
    IpCdg00AreaRepeatSrcClass* source_class) {
    auto* object_class      = G_OBJECT_CLASS(source_class);
    auto* element_class     = GST_ELEMENT_CLASS(source_class);
    auto* base_source_class = GST_BASE_SRC_CLASS(source_class);
    auto* push_source_class = GST_PUSH_SRC_CLASS(source_class);

    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyLocation,
        g_param_spec_string("location", "Location", "CDG0.0 input file", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyImageHeight,
        g_param_spec_int("image-height", "Image height", "Output image height",
                         1, 16384, 4096,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyRepeatCount,
        g_param_spec_int("repeat-count", "Repeat count",
                         "Number of identical area-array frames to emit", 1,
                         4096, 3,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyFps,
        g_param_spec_double("fps", "FPS", "Output frame rate", 0.1, 1000.0,
                            30.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process CDG0.0 Area Repeat Source",
        "Source/Video/File",
        "Repeats the first CDG0.0 window as identical area-array frames",
        "image-process maintainers");
    static GstStaticPadTemplate source_template = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string)GRAY8, width=(int)4096, "
                        "height=(int)[1,16384], "
                        "framerate=(fraction)[1/10,1000/1]"));
    gst_element_class_add_static_pad_template(element_class, &source_template);

    base_source_class->start    = start;
    base_source_class->stop     = stop;
    base_source_class->get_caps = get_caps;
    push_source_class->create   = create;
}

static void ip_cdg00_area_repeat_src_init(IpCdg00AreaRepeatSrc* source) {
    g_object_set_qdata_full(
        G_OBJECT(source), state_quark(), new AreaRepeatState(),
        [](gpointer data) { delete static_cast<AreaRepeatState*>(data); });
    gst_base_src_set_format(GST_BASE_SRC(source), GST_FORMAT_TIME);
}
