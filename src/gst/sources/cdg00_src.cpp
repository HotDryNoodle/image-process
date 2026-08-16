/*
 * Portions refactored from MSF Project source at revision c4046d66.
 * Copyright (C) 2025 MSF Project.
 * See SOURCE_PROVENANCE.json and NOTICE for origin and license details.
 */
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "image_process/gst_meta_v1.h"

namespace {

constexpr std::size_t                 kLineSize       = 6448;
constexpr std::size_t                 kLineHeaderSize = 16;
constexpr std::size_t                 kLineParamSize  = 32;
constexpr std::size_t                 kImageWidth     = 4096;
constexpr std::size_t                 kPackedLineSize = 5120;
constexpr std::array<std::uint8_t, 5> kMagic = {0xFA, 0xF3, 0x34, 0x0A, 0x01};

std::uint16_t read_be16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) << 8U |
           static_cast<std::uint16_t>(data[1]);
}

std::uint32_t read_be32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) << 24U |
           static_cast<std::uint32_t>(data[1]) << 16U |
           static_cast<std::uint32_t>(data[2]) << 8U |
           static_cast<std::uint32_t>(data[3]);
}

float read_be_float(const std::uint8_t* data) {
    const std::uint32_t bits = read_be32(data);
    float               value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool valid_line_header(const std::uint8_t* data, std::uint8_t& row_number) {
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) { return false; }
    row_number = data[9] & 0x0FU;
    return true;
}

IpCdg00SampleV1 empty_sample() {
    IpCdg00SampleV1 sample{};
    sample.struct_size = sizeof(sample);
    return sample;
}

IpCdg00SampleV1 parse_sample(const std::uint8_t* data) {
    IpCdg00SampleV1 sample     = empty_sample();
    sample.valid               = 1;
    sample.channel_id          = data[5];
    sample.strip_number        = data[6];
    sample.row_number          = read_be32(data + 6) & UINT32_C(0x00FFFFFF);
    sample.camera_microseconds = read_be32(data + 10) >> 8U;

    const std::uint8_t* line_0 = data + kLineHeaderSize;
    sample.camera_seconds      = read_be32(line_0);
    sample.time_sync_status    = line_0[4];
    sample.exposure_time_ns    = static_cast<std::uint32_t>(
        static_cast<double>(read_be32(line_0 + 4) >> 8U) * 12.5);

    const std::uint8_t* line_14 = data + kLineHeaderSize + 14U * kLineSize;
    const std::uint8_t* line_15 = data + kLineHeaderSize + 15U * kLineSize;
    sample.gps_week             = read_be16(line_14);
    sample.gps_seconds          = read_be32(line_14 + 2);
    for (std::size_t index = 0; index < 3; ++index) {
        sample.lla[index]      = read_be_float(line_14 + 6U + index * 4U);
        sample.velocity[index] = read_be_float(line_14 + 18U + index * 4U);
    }
    std::array<std::uint8_t, 4> roll = {line_14[30], line_14[31], line_15[0],
                                        line_15[1]};
    sample.attitude[0]               = read_be_float(roll.data());
    sample.attitude[1]               = read_be_float(line_15 + 2);
    sample.attitude[2]               = read_be_float(line_15 + 6);
    return sample;
}

std::size_t unpack_10_to_8(const std::uint8_t* input,
                           std::size_t         input_size,
                           std::uint8_t*       output,
                           std::size_t         output_size) {
    const std::size_t pack_count   = input_size / 5U;
    const std::size_t pixel_count  = std::min(output_size, pack_count * 4U);
    std::size_t       output_index = 0;
    for (std::size_t pack = 0; pack < pack_count && output_index < pixel_count;
         ++pack) {
        const std::uint8_t*                bytes  = input + pack * 5U;
        const std::array<std::uint16_t, 4> pixels = {
            static_cast<std::uint16_t>(bytes[0] << 2U | bytes[1] >> 6U),
            static_cast<std::uint16_t>((bytes[1] & 0x3FU) << 4U |
                                       bytes[2] >> 4U),
            static_cast<std::uint16_t>((bytes[2] & 0x0FU) << 6U |
                                       bytes[3] >> 2U),
            static_cast<std::uint16_t>((bytes[3] & 0x03U) << 8U | bytes[4]),
        };
        for (const std::uint16_t pixel : pixels) {
            if (output_index >= pixel_count) { break; }
            output[output_index++] = static_cast<std::uint8_t>(pixel >> 2U);
        }
    }
    return output_index;
}

struct Cdg00SourceState {
    std::string   location;
    std::ifstream stream;
    std::uint64_t file_size    = 0;
    std::uint64_t offset       = 0;
    std::uint64_t frame_number = 0;
    int           channel      = 0;
    int           image_height = 4096;
    int           stride_lines = 32;
    double        fps          = 30.0;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-cdg00-source-state");
}

}  // namespace

typedef struct _IpCdg00Src {
    GstPushSrc parent;
} IpCdg00Src;

typedef struct _IpCdg00SrcClass {
    GstPushSrcClass parent_class;
} IpCdg00SrcClass;

G_DEFINE_TYPE(IpCdg00Src, ip_cdg00_src, GST_TYPE_PUSH_SRC)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyLocation,
    kPropertyChannel,
    kPropertyImageHeight,
    kPropertyStrideLines,
    kPropertyFps,
};

Cdg00SourceState* state(IpCdg00Src* source) {
    return static_cast<Cdg00SourceState*>(
        g_object_get_qdata(G_OBJECT(source), state_quark()));
}

GstCaps* make_caps(const Cdg00SourceState& value_state) {
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
    const auto* value_state = state(reinterpret_cast<IpCdg00Src*>(base_source));
    GstCaps*    caps        = make_caps(*value_state);
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
    auto* source      = reinterpret_cast<IpCdg00Src*>(object);
    auto* value_state = state(source);
    switch (property_id) {
        case kPropertyLocation:
            value_state->location = g_value_get_string(value) == nullptr
                                        ? ""
                                        : g_value_get_string(value);
            break;
        case kPropertyChannel:
            value_state->channel = g_value_get_int(value);
            break;
        case kPropertyImageHeight:
            value_state->image_height = g_value_get_int(value);
            break;
        case kPropertyStrideLines:
            value_state->stride_lines = g_value_get_int(value);
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
    auto* value_state = state(reinterpret_cast<IpCdg00Src*>(object));
    switch (property_id) {
        case kPropertyLocation:
            g_value_set_string(value, value_state->location.c_str());
            break;
        case kPropertyChannel:
            g_value_set_int(value, value_state->channel);
            break;
        case kPropertyImageHeight:
            g_value_set_int(value, value_state->image_height);
            break;
        case kPropertyStrideLines:
            g_value_set_int(value, value_state->stride_lines);
            break;
        case kPropertyFps:
            g_value_set_double(value, value_state->fps);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

gboolean start(GstBaseSrc* base_source) {
    auto* source      = reinterpret_cast<IpCdg00Src*>(base_source);
    auto* value_state = state(source);
    if (value_state->channel != 0) {
        GST_ELEMENT_ERROR(source, RESOURCE, SETTINGS,
                          ("CDG00Src currently supports only channel P"),
                          ("channel=%d", value_state->channel));
        return FALSE;
    }
    if (value_state->location.empty()) {
        GST_ELEMENT_ERROR(source, RESOURCE, NOT_FOUND,
                          ("CDG00Src location is empty"), (nullptr));
        return FALSE;
    }
    value_state->stream.open(value_state->location, std::ios::binary);
    if (!value_state->stream) {
        GST_ELEMENT_ERROR(source, RESOURCE, OPEN_READ,
                          ("cannot open CDG0.0 input"),
                          ("location=%s", value_state->location.c_str()));
        return FALSE;
    }
    value_state->stream.seekg(0, std::ios::end);
    const std::streamoff end = value_state->stream.tellg();
    if (end < 0) {
        GST_ELEMENT_ERROR(source, RESOURCE, READ,
                          ("cannot determine CDG0.0 input size"), (nullptr));
        return FALSE;
    }
    value_state->file_size    = static_cast<std::uint64_t>(end);
    value_state->offset       = 0;
    value_state->frame_number = 0;
    value_state->stream.clear();

    GstCaps*       caps     = make_caps(*value_state);
    const gboolean accepted = gst_base_src_set_caps(base_source, caps);
    gst_caps_unref(caps);
    return accepted;
}

gboolean stop(GstBaseSrc* base_source) {
    auto* value_state = state(reinterpret_cast<IpCdg00Src*>(base_source));
    if (value_state->stream.is_open()) { value_state->stream.close(); }
    value_state->file_size    = 0;
    value_state->offset       = 0;
    value_state->frame_number = 0;
    return TRUE;
}

GstFlowReturn create(GstPushSrc* push_source, GstBuffer** output_buffer) {
    auto* source      = reinterpret_cast<IpCdg00Src*>(push_source);
    auto* value_state = state(source);
    if (value_state->offset >= value_state->file_size) { return GST_FLOW_EOS; }

    const std::size_t block_size =
        kLineSize * static_cast<std::size_t>(value_state->image_height);
    const std::size_t available =
        static_cast<std::size_t>(std::min<std::uint64_t>(
            block_size, value_state->file_size - value_state->offset));
    if (available < kLineSize) { return GST_FLOW_EOS; }

    std::vector<std::uint8_t> raw(available);
    value_state->stream.clear();
    value_state->stream.seekg(static_cast<std::streamoff>(value_state->offset),
                              std::ios::beg);
    value_state->stream.read(reinterpret_cast<char*>(raw.data()),
                             static_cast<std::streamsize>(raw.size()));
    const std::size_t bytes_read =
        static_cast<std::size_t>(value_state->stream.gcount());
    if (bytes_read < kLineSize) { return GST_FLOW_EOS; }
    raw.resize(bytes_read);
    value_state->offset +=
        kLineSize * static_cast<std::uint64_t>(value_state->stride_lines);

    const std::size_t         line_count = raw.size() / kLineSize;
    std::vector<std::uint8_t> converted(
        kImageWidth * static_cast<std::size_t>(value_state->image_height), 0);
    std::size_t   converted_size = 0;
    IpCdg00MetaV1 metadata{};
    metadata.abi_version  = IP_GST_META_ABI_VERSION_V1;
    metadata.struct_size  = sizeof(metadata);
    metadata.window_start = empty_sample();
    metadata.window_end   = empty_sample();

    for (std::size_t line = 0; line < line_count; ++line) {
        const std::uint8_t* line_data  = raw.data() + line * kLineSize;
        std::uint8_t        row_number = 0;
        if (!valid_line_header(line_data, row_number)) { continue; }
        const bool sample_fits = line + 16U <= line_count;
        if (sample_fits && row_number == 0U) {
            const IpCdg00SampleV1 sample = parse_sample(line_data);
            if (metadata.window_start.valid == 0U) {
                metadata.window_start = sample;
            }
            metadata.window_end = sample;
        }

        const std::size_t written =
            unpack_10_to_8(line_data + kLineHeaderSize + kLineParamSize,
                           kPackedLineSize, converted.data() + converted_size,
                           converted.size() - converted_size);
        converted_size += written;
    }
    if (converted_size == 0U) {
        GST_ELEMENT_ERROR(source, STREAM, DECODE,
                          ("CDG0.0 block contains no valid image lines"),
                          ("offset=%" G_GUINT64_FORMAT, value_state->offset));
        return GST_FLOW_ERROR;
    }

    GstBuffer* buffer =
        gst_buffer_new_allocate(nullptr, converted.size(), nullptr);
    if (buffer == nullptr) { return GST_FLOW_ERROR; }
    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    std::memcpy(map.data, converted.data(), converted.size());
    gst_buffer_unmap(buffer, &map);

    const GstClockTime duration = static_cast<GstClockTime>(
        static_cast<double>(GST_SECOND) / value_state->fps);
    GST_BUFFER_PTS(buffer)        = value_state->frame_number * duration;
    GST_BUFFER_OFFSET(buffer)     = value_state->frame_number;
    GST_BUFFER_OFFSET_END(buffer) = value_state->frame_number + 1U;
    ++value_state->frame_number;
    if (!ip_buffer_add_cdg00_meta(buffer, &metadata)) {
        gst_buffer_unref(buffer);
        GST_ELEMENT_ERROR(source, STREAM, FAILED,
                          ("failed to attach CDG0.0 metadata"), (nullptr));
        return GST_FLOW_ERROR;
    }
    *output_buffer = buffer;
    return GST_FLOW_OK;
}

}  // namespace

static void ip_cdg00_src_class_init(IpCdg00SrcClass* source_class) {
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
        object_class, kPropertyChannel,
        g_param_spec_int("channel", "Channel", "0=P, 1=B1, 2=B2, 3=B3, 4=B4", 0,
                         4, 0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyImageHeight,
        g_param_spec_int("image-height", "Image height", "Output image height",
                         1, 16384, 4096,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyStrideLines,
        g_param_spec_int("stride-lines", "Stride lines",
                         "Input lines advanced per output frame", 1, 16384, 32,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyFps,
        g_param_spec_double("fps", "FPS", "Output frame rate", 0.1, 1000.0,
                            30.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process CDG0.0 Source", "Source/Video/File",
        "Reads bounded CDG0.0 windows with a versioned metadata ABI",
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

static void ip_cdg00_src_init(IpCdg00Src* source) {
    g_object_set_qdata_full(
        G_OBJECT(source), state_quark(), new Cdg00SourceState(),
        [](gpointer data) { delete static_cast<Cdg00SourceState*>(data); });
    gst_base_src_set_format(GST_BASE_SRC(source), GST_FORMAT_TIME);
}
