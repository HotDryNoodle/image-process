#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "image_process/gst_meta_v1.h"

namespace image_process {
namespace cdg00 {

constexpr std::size_t                 kLineSize       = 6448;
constexpr std::size_t                 kLineHeaderSize = 16;
constexpr std::size_t                 kLineParamSize  = 32;
constexpr std::size_t                 kImageWidth     = 4096;
constexpr std::size_t                 kPackedLineSize = 5120;
constexpr std::array<std::uint8_t, 5> kMagic = {0xFA, 0xF3, 0x34, 0x0A, 0x01};

inline std::uint16_t read_be16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) << 8U |
           static_cast<std::uint16_t>(data[1]);
}

inline std::uint32_t read_be32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) << 24U |
           static_cast<std::uint32_t>(data[1]) << 16U |
           static_cast<std::uint32_t>(data[2]) << 8U |
           static_cast<std::uint32_t>(data[3]);
}

inline float read_be_float(const std::uint8_t* data) {
    const std::uint32_t bits = read_be32(data);
    float               value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline bool valid_line_header(const std::uint8_t* data,
                              std::uint8_t&       row_number) {
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) { return false; }
    row_number = data[9] & 0x0FU;
    return true;
}

inline IpCdg00SampleV1 empty_sample() {
    IpCdg00SampleV1 sample{};
    sample.struct_size = sizeof(sample);
    return sample;
}

inline IpCdg00SampleV1 parse_sample(const std::uint8_t* data) {
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

inline std::size_t unpack_10_to_8(const std::uint8_t* input,
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

struct DecodedWindow {
    std::vector<std::uint8_t> pixels;
    IpCdg00MetaV1             metadata{};
};

inline bool decode_window(const std::vector<std::uint8_t>& raw,
                          int                              image_height,
                          DecodedWindow&                   decoded) {
    const std::size_t line_count = raw.size() / kLineSize;
    decoded.pixels.assign(kImageWidth * static_cast<std::size_t>(image_height),
                          0);
    std::size_t converted_size    = 0;
    decoded.metadata              = {};
    decoded.metadata.abi_version  = IP_GST_META_ABI_VERSION_V1;
    decoded.metadata.struct_size  = sizeof(decoded.metadata);
    decoded.metadata.window_start = empty_sample();
    decoded.metadata.window_end   = empty_sample();

    for (std::size_t line = 0; line < line_count; ++line) {
        const std::uint8_t* line_data  = raw.data() + line * kLineSize;
        std::uint8_t        row_number = 0;
        if (!valid_line_header(line_data, row_number)) { continue; }
        const bool sample_fits = line + 16U <= line_count;
        if (sample_fits && row_number == 0U) {
            const IpCdg00SampleV1 sample = parse_sample(line_data);
            if (decoded.metadata.window_start.valid == 0U) {
                decoded.metadata.window_start = sample;
            }
            decoded.metadata.window_end = sample;
        }
        const std::size_t written = unpack_10_to_8(
            line_data + kLineHeaderSize + kLineParamSize, kPackedLineSize,
            decoded.pixels.data() + converted_size,
            decoded.pixels.size() - converted_size);
        converted_size += written;
    }
    return converted_size > 0U;
}

inline IpAreaFrameMetaV1 area_frame_from_sample(const IpCdg00SampleV1& sample) {
    IpAreaFrameMetaV1 frame{};
    frame.abi_version         = IP_GST_META_ABI_VERSION_V1;
    frame.struct_size         = sizeof(frame);
    frame.valid               = sample.valid;
    frame.channel_id          = sample.channel_id;
    frame.strip_number        = sample.strip_number;
    frame.time_sync_status    = sample.time_sync_status;
    frame.row_number          = sample.row_number;
    frame.camera_seconds      = sample.camera_seconds;
    frame.camera_microseconds = sample.camera_microseconds;
    frame.exposure_time_ns    = sample.exposure_time_ns;
    return frame;
}

}  // namespace cdg00
}  // namespace image_process
