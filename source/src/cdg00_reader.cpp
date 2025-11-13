/**
 * @file cdg00_reader.cpp
 * @brief CDG0.0 Format Reader Implementation
 * @copyright Copyright (C) 2025 MSF Project
 */

#include "cdg00_reader.hpp"

#include <algorithm>
#include <cstring>

extern "C" {
#include <gst/video/video.h>
}

namespace msf {

CDG00Reader::CDG00Reader()
    : initialized_(false),
      block_size_(0),
      stride_offset_(0),
      conversion_buffer_() {
    Initialize();
}

CDG00Reader::~CDG00Reader() {
    Close();
}

bool CDG00Reader::Initialize() {
    if (initialized_) {
        return true;
    }

    // Calculate block size based on CDG format
    // Each line: header + params + video data + padding
    const gsize line_video_size = kImageWidth * kPixelDepth / 8;
    const gsize total_line_size =
        kLineHeaderSize + kLineParamSize + line_video_size + kLineDataPadding;

    block_size_ = total_line_size * kImageHeight;
    stride_offset_ = kDefaultStrideLines * total_line_size;
    // Pre-allocate conversion buffer for 10-bit to 8-bit conversion
    const gsize max_output_size = kImageWidth * kImageHeight;
    conversion_buffer_.reserve(max_output_size);

    initialized_ = true;
    return true;
}

bool CDG00Reader::ReadHeader() {
    // CDG0.0 format typically doesn't have a separate file header
    // Header information is embedded in each line header
    return true;
}

GstBuffer* CDG00Reader::ProcessFrame(const guint8* data,
                                     gsize         size,
                                     GstCaps*      caps) {
    GST_DEBUG("ProcessFrame called: initialized=%s, data=%p, size=%zu", 
              initialized_ ? "TRUE" : "FALSE", data, size);
    
    if (!initialized_) {
        GST_ERROR("CDG00Reader not initialized!");
        return nullptr;
    }
    
    if (!data || size == 0) {
        GST_ERROR("Invalid data or size: data=%p, size=%zu", data, size);
        return nullptr;
    }

    // Check if we have enough data for a complete frame (block_size_)
    if (size < block_size_) {
        GST_WARNING("Insufficient data for CDG frame processing: got %zu, expected %zu", 
                    size, block_size_);
        return nullptr;
    }

    // Process the entire frame data (all lines)
    return ProcessVideoData(data, size, caps);
}

void CDG00Reader::SetBinFileSrcProperty(BinFileSrc* src) {
    if (!src) {
        return;
    }

    // Set CDG0.0 specific properties according to design specification
    src->each_block_size = block_size_;       // Each data block size
    src->stride_offset   = stride_offset_;    // Stride offset for reading
    
    // Set format-specific flags
    src->has_header      = FALSE;             // No separate file header
    src->header_size     = 0;                 // No header
}

void CDG00Reader::Close() {
    conversion_buffer_.clear();
    initialized_ = false;
}

gsize CDG00Reader::GetBlockSize() const {
    return block_size_;
}

gint64 CDG00Reader::GetStrideOffset() const {
    return stride_offset_;
}

bool CDG00Reader::ParseLineHeader(const guint8* data, CDG0LineHeader* header) {
    if (!data || !header) {
        return false;
    }

    // Parse magic number (5 bytes)
    header->magic = 0;
    for (int i = 0; i < 5; ++i) {
        header->magic |= (static_cast<guint64>(data[i]) << (i * 8));
    }

    // Parse row ID (3 bytes at offset 7)
    header->row_id = 0;
    for (int i = 0; i < 3; ++i) {
        header->row_id |= (static_cast<guint32>(data[7 + i]) << (i * 8));
    }

    // Extract package index (low 4 bits of row_id)
    header->package_index = header->row_id & 0x0F;

    // Check if this is frame start (package_index == 0)
    header->is_frame_start = (header->package_index == 0);

    return true;
}

GstBuffer* CDG00Reader::ProcessVideoData(const guint8* data,
                                         gsize         size,
                                         GstCaps*      caps) {
    if (!data || size == 0) {
        return nullptr;
    }
    // print caps
    GST_LOG("caps are %" GST_PTR_FORMAT, caps);
    // Calculate line sizes based on CDG format (same as demuxer.c)
    const gsize line_video_size = kImageWidth * kPixelDepth / 8;
    const gsize total_line_size = kLineHeaderSize + kLineParamSize + line_video_size + kLineDataPadding;
    
    // Calculate expected number of lines
    const guint num_lines = size / total_line_size;
    if (num_lines == 0) {
        GST_WARNING("Data size %zu too small for even one line (expected %zu per line)", 
                    size, total_line_size);
        return nullptr;
    }
    
    // Prepare output buffer for entire frame (all lines converted to 8-bit)
    const gsize frame_output_size = kImageWidth * num_lines;  // 8-bit per pixel
    conversion_buffer_.resize(frame_output_size);
    
    gsize total_converted = 0;
    const guint8* current_line_ptr = data;
    
    GST_DEBUG("Processing %u lines, total_line_size=%zu, line_video_size=%zu", 
              num_lines, total_line_size, line_video_size);
    
    // Process each line in the frame
    for (guint line = 0; line < num_lines; ++line) {
        // Check if we have enough data for this line
        if ((current_line_ptr - data) + total_line_size > size) {
            GST_WARNING("Insufficient data for line %u", line);
            break;
        }
        
        // Parse line header to verify data integrity
        CDG0LineHeader line_header;
        if (!ParseLineHeader(current_line_ptr, &line_header)) {
            GST_DEBUG("Failed to parse header for line %u, skipping", line);
            current_line_ptr += total_line_size;
            continue;
        }
        
        // Skip to video data (past header and params)
        const guint8* line_video_data = current_line_ptr + kLineHeaderSize + kLineParamSize;
        
        // Convert 10-bit line data to 8-bit
        const gsize line_converted = Convert10bitTo8bit(
            line_video_data, 
            line_video_size,
            conversion_buffer_.data() + total_converted, 
            frame_output_size - total_converted);
            
        if (line_converted == 0) {
            GST_WARNING("Failed to convert line %u video data", line);
        } else {
            total_converted += line_converted;
            GST_DEBUG("Converted line %u: %zu bytes -> %zu bytes", 
                      line, line_video_size, line_converted);
        }
        
        // Move to next line
        current_line_ptr += total_line_size;
    }
    
    if (total_converted == 0) {
        GST_ERROR("No video data was successfully converted");
        return nullptr;
    }

    // Create GStreamer buffer for the complete frame
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, total_converted, nullptr);
    if (!buffer) {
        GST_ERROR("Failed to allocate GstBuffer of size %zu", total_converted);
        return nullptr;
    }

    // Fill buffer with all converted frame data
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        std::memcpy(map.data, conversion_buffer_.data(), total_converted);
        gst_buffer_unmap(buffer, &map);
    } else {
        gst_buffer_unref(buffer);
        GST_ERROR("Failed to map GstBuffer for writing");
        return nullptr;
    }

    GST_DEBUG("Successfully processed frame: %u lines, %zu total bytes output", 
              num_lines, total_converted);

    return buffer;
}

gsize CDG00Reader::Convert10bitTo8bit(const guint8* input_data,
                                      gsize         input_size,
                                      guint8*       output_buffer,
                                      gsize         output_size) {
    if (!input_data || !output_buffer || input_size == 0 || output_size == 0) {
        return 0;
    }

    // 10-bit data is packed: 4 pixels = 5 bytes
    // Each pixel is 10 bits, we extract the high 8 bits for 8-bit output
    const gsize pixels_per_pack   = 4;
    const gsize bytes_per_pack    = 5;
    const gsize num_packs         = input_size / bytes_per_pack;
    const gsize max_output_pixels = std::min(output_size, num_packs * pixels_per_pack);

    gsize output_index = 0;

    for (gsize pack = 0; pack < num_packs && output_index < max_output_pixels; ++pack) {
        const guint8* pack_data = input_data + (pack * bytes_per_pack);

        // Extract 4 10-bit pixels from 5 bytes
        // Pixel layout: [B0][B1][B2][B3][B4]
        const guint16 p0 = (pack_data[0] << 2) | (pack_data[1] >> 6);
        const guint16 p1 = ((pack_data[1] & 0x3F) << 4) | (pack_data[2] >> 4);
        const guint16 p2 = ((pack_data[2] & 0x0F) << 6) | (pack_data[3] >> 2);
        const guint16 p3 = ((pack_data[3] & 0x03) << 8) |  pack_data[4];

        // Convert to 8-bit by taking high 8 bits
        if (output_index < max_output_pixels) output_buffer[output_index++] = p0 >> 2;
        if (output_index < max_output_pixels) output_buffer[output_index++] = p1 >> 2;
        if (output_index < max_output_pixels) output_buffer[output_index++] = p2 >> 2;
        if (output_index < max_output_pixels) output_buffer[output_index++] = p3 >> 2;
    }

    return output_index;
}

}  // namespace msf
