/**
 * @file cdg00_reader.cpp
 * @brief CDG0.0 Format Reader Implementation
 * @copyright Copyright (C) 2025 MSF Project
 */

#include "cdg00_reader.hpp"
#include "format_factory.hpp"
#include "bin_src_impl.hpp"
#include <algorithm>
#include <cstring>

extern "C" {
#include <gst/video/video.h>
}

namespace msf {

// Define and register CDG00Src GStreamer element
DEFINE_GST_FORMAT_ELEMENT_WITH_PROPERTIES(
    msf::CDG00Reader,        // ReaderClass
    "cdg00",                 // TypeName
    CDG00Src,                // ElementClassName
    CDG00Src,                // element_name
    CDG00_SRC,               // ELEMENT_NAME
    "CDG0.0 Source",         // LongName
    "Source/Video/File",     // Classification
    "Read CDG0.0 format remote sensing data files", // Description
    "MSF Project"            // Author
);

CDG00Reader::CDG00Reader()
    : initialized_(false),
      block_size_(0),
      stride_offset_(0),
      conversion_buffer_(),
      channel_(CHANNEL_P),
      stride_lines_(kDefaultStrideLines),
      image_width_(kDefaultImageWidth),
      image_height_(kDefaultImageHeight) {
    Initialize();
}

CDG00Reader::~CDG00Reader() {
    Close();
}

bool CDG00Reader::Initialize() {
    if (initialized_) {
        return true;
    }

    RecalculateParameters();
    initialized_ = true;
    return true;
}

void CDG00Reader::RecalculateParameters() {
    // Calculate block size based on CDG format
    // Each line: header + params + video data + padding
    const gsize line_video_size = image_width_ * kPixelDepth / 8;
    const gsize total_line_size =
        kLineHeaderSize + kLineParamSize + line_video_size + kLineDataPadding;

    block_size_ = total_line_size * image_height_;
    stride_offset_ = stride_lines_ * total_line_size;
    
    // Pre-allocate conversion buffer for 10-bit to 8-bit conversion
    const gsize max_output_size = image_width_ * image_height_;
    conversion_buffer_.reserve(max_output_size);
    
    GST_DEBUG("CDG00Reader parameters recalculated: "
              "channel=%d, stride_lines=%u, image_width=%u, image_height=%u, "
              "block_size=%zu, stride_offset=%lld",
              channel_, stride_lines_, image_width_, image_height_,
              block_size_, (long long)stride_offset_);
}

bool CDG00Reader::ReadHeader() {
    // CDG0.0 format typically doesn't have a separate file header
    // Header information is embedded in each line header
    return true;
}

PropertyDefinitions CDG00Reader::GetPropertyDefinitions() const {
    PropertyDefinitions defs;
    
    // Channel property (enum as integer)
    PropertyDefinition channel_def;
    channel_def.name = "channel";
    channel_def.type = PROPERTY_TYPE_INTEGER;
    channel_def.default_value = PropertyValue(static_cast<int>(CHANNEL_P));
    channel_def.description = "Channel selection (0=P, 1=B1, 2=B2, 3=B3, 4=B4)";
    channel_def.min_value = PropertyValue(static_cast<int>(CHANNEL_P));
    channel_def.max_value = PropertyValue(static_cast<int>(CHANNEL_B4));
    defs.push_back(channel_def);
    
    // Stride lines property
    PropertyDefinition stride_def;
    stride_def.name = "stride-lines";
    stride_def.type = PROPERTY_TYPE_INTEGER;
    stride_def.default_value = PropertyValue(static_cast<int>(kDefaultStrideLines));
    stride_def.description = "Number of lines to stride per read";
    stride_def.min_value = PropertyValue(1);
    stride_def.max_value = PropertyValue(1024);
    defs.push_back(stride_def);
    
    // Image height property
    PropertyDefinition height_def;
    height_def.name = "image-height";
    height_def.type = PROPERTY_TYPE_INTEGER;
    height_def.default_value = PropertyValue(static_cast<int>(kDefaultImageHeight));
    height_def.description = "Height of the image to read";
    height_def.min_value = PropertyValue(1);
    height_def.max_value = PropertyValue(16384);
    defs.push_back(height_def);
    
    return defs;
}

bool CDG00Reader::SetProperty(const std::string& name, const PropertyValue& value) {
    if (name == "channel") {
        int channel_val = value.Get<int>();
        if (channel_val < CHANNEL_P || channel_val > CHANNEL_B4) {
            GST_WARNING("Invalid channel value: %d", channel_val);
            return false;
        }
        channel_ = static_cast<CDG00Channel>(channel_val);
        GST_INFO("Channel set to: %d", channel_);
        return true;
    }
    else if (name == "stride-lines") {
        int stride_val = value.Get<int>();
        if (stride_val < 1 || stride_val > 1024) {
            GST_WARNING("Invalid stride-lines value: %d", stride_val);
            return false;
        }
        stride_lines_ = static_cast<guint>(stride_val);
        RecalculateParameters();
        GST_INFO("Stride lines set to: %u", stride_lines_);
        return true;
    }
    else if (name == "image-height") {
        int height_val = value.Get<int>();
        if (height_val < 1 || height_val > 16384) {
            GST_WARNING("Invalid image-height value: %d", height_val);
            return false;
        }
        image_height_ = static_cast<guint>(height_val);
        RecalculateParameters();
        GST_INFO("Image height set to: %u", image_height_);
        return true;
    }
    
    GST_WARNING("CDG00Reader: Property '%s' not found", name.c_str());
    return false;
}

PropertyValue CDG00Reader::GetProperty(const std::string& name) const {
    if (name == "channel") {
        return PropertyValue(static_cast<int>(channel_));
    }
    else if (name == "stride-lines") {
        return PropertyValue(static_cast<int>(stride_lines_));
    }
    else if (name == "image-height") {
        return PropertyValue(static_cast<int>(image_height_));
    }
    
    GST_WARNING("CDG00Reader: Property '%s' not found", name.c_str());
    return PropertyValue("");
}

GstCaps* CDG00Reader::GetCaps() {
    if (!initialized_) {
        GST_WARNING("CDG00Reader not initialized, using default dimensions");
    }
    
    // Create caps for GRAY8 format with current image dimensions
    GstVideoInfo info;
    gst_video_info_init(&info);
    gst_video_info_set_format(&info, GST_VIDEO_FORMAT_GRAY8, 
                              image_width_, image_height_);
    
    GstCaps* caps = gst_video_info_to_caps(&info);
    
    // Remove framerate to allow downstream negotiation
    GstStructure* s = gst_caps_get_structure(caps, 0);
    gst_structure_remove_field(s, "framerate");
    
    GST_INFO("CDG00Reader generated caps: %" GST_PTR_FORMAT 
             " (width=%u, height=%u)", 
             caps, image_width_, image_height_);
    
    return caps;
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

    // Allow processing of partial frames (e.g., last frame in file may be incomplete)
    // Calculate line size to determine minimum required data
    const gsize line_video_size = image_width_ * kPixelDepth / 8;
    const gsize total_line_size = kLineHeaderSize + kLineParamSize + line_video_size + kLineDataPadding;
    
    if (size < total_line_size) {
        GST_WARNING("Data size %zu is less than one complete line (%zu bytes), cannot process", 
                    size, total_line_size);
        return nullptr;
    }
    
    if (size < block_size_) {
        GST_INFO("Partial frame detected: got %zu bytes, expected %zu (processing available data)", 
                 size, block_size_);
    }

    // Process the frame data (all complete lines available)
    // Currently only supports channel P (Panchromatic)
    return ProcessVideoDataChannelP(data, size, caps);
}

// Note: SetBinFileSrcProperty removed - BinFileSrc now queries via GetBlockSize/GetStrideOffset

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

GstBuffer* CDG00Reader::ProcessVideoDataChannelP(const guint8* data,
                                                 gsize         size,
                                                 GstCaps*      caps) {
    if (!data || size == 0) {
        return nullptr;
    }
    // print caps
    GST_LOG("caps are %" GST_PTR_FORMAT, caps);
    // Calculate line sizes based on CDG format (same as demuxer.c)
    const gsize line_video_size = image_width_ * kPixelDepth / 8;
    const gsize total_line_size = kLineHeaderSize + kLineParamSize + line_video_size + kLineDataPadding;
    
    // Calculate expected number of lines
    const guint num_lines = size / total_line_size;
    if (num_lines == 0) {
        GST_WARNING("Data size %zu too small for even one line (expected %zu per line)", 
                    size, total_line_size);
        return nullptr;
    }
    
    // Prepare output buffer for entire frame (all lines converted to 8-bit)
    const gsize frame_output_size = image_width_ * num_lines;  // 8-bit per pixel
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

    // Calculate expected frame size based on image dimensions
    const gsize expected_frame_size = image_width_ * image_height_;
    
    // Create GStreamer buffer with expected size (may need padding)
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, expected_frame_size, nullptr);
    if (!buffer) {
        GST_ERROR("Failed to allocate GstBuffer of size %zu", expected_frame_size);
        return nullptr;
    }

    // Fill buffer with converted frame data
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        // Copy converted data
        std::memcpy(map.data, conversion_buffer_.data(), total_converted);
        
        // Pad with zeros if data is less than expected (e.g., last frame)
        if (total_converted < expected_frame_size) {
            gsize padding_size = expected_frame_size - total_converted;
            std::memset(map.data + total_converted, 0, padding_size);
            GST_INFO("Padded partial frame: %zu bytes converted, %zu bytes padded (total %zu)", 
                     total_converted, padding_size, expected_frame_size);
        }
        
        gst_buffer_unmap(buffer, &map);
    } else {
        gst_buffer_unref(buffer);
        GST_ERROR("Failed to map GstBuffer for writing");
        return nullptr;
    }

    GST_DEBUG("Successfully processed frame: %u lines converted, %zu total bytes output", 
              num_lines, expected_frame_size);

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
