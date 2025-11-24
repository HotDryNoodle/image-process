/**
 * @file cdg00_reader.hpp
 * @brief CDG0.0 Format Reader Implementation
 * @copyright Copyright (C) 2025 MSF Project
 */

#ifndef MSF_CDG00_READER_HPP
#define MSF_CDG00_READER_HPP

#include <memory>
#include <vector>
#include <gst/gst.h>

#include "format_reader_interface.hpp"

namespace msf {

// Forward declaration - we don't need the full BinFileSrc structure
struct _BinFileSrc;

/**
 * @brief Channel enumeration for CDG0.0 format
 */
enum CDG00Channel {
    CHANNEL_P = 0,   // 全色谱
    CHANNEL_B1 = 1,  // B1 谱段
    CHANNEL_B2 = 2,  // B2 谱段
    CHANNEL_B3 = 3,  // B3 谱段
    CHANNEL_B4 = 4   // B4 谱段
};

/**
 * @brief CDG0.0 format reader implementation
 *
 * This class handles the CDG0.0 remote sensing data format,
 * based on the processing logic from demuxer.c
 */
class CDG00Reader : public IFormatReader {
public:
    CDG00Reader();
    ~CDG00Reader() override;

    // IFormatReader interface implementation
    bool        ReadHeader() override;
    GstBuffer*  ProcessFrame(const guint8* data,
                             gsize         size,
                             GstCaps*      caps) override;
    void        Close() override;
    gsize       GetBlockSize() const override;
    gint64      GetStrideOffset() const override;
    
    // Property system support
    PropertyDefinitions GetPropertyDefinitions() const override;
    bool SetProperty(const std::string& name, const PropertyValue& value) override;
    PropertyValue GetProperty(const std::string& name) const override;
    
    // Caps generation
    GstCaps* GetCaps() override;

protected:
    bool Initialize() override;

    /**
     * @brief Parse CDG line header structure
     * @param data Raw data pointer
     * @param header Output header structure  
     * @return true if parsed successfully
     */
    struct CDG0LineHeader {
        guint64  magic;            // 魔数 (实际5字节)
        guint32  row_id;           // 行号标识 (实际3字节)
        guint8   package_index;    // 包索引 (row_id的低4bit)
        gboolean is_frame_start;   // 是否帧起始行
    };
    
    bool ParseLineHeader(const guint8* data, CDG0LineHeader* header);

    /**
     * @brief Process video data for channel P (Panchromatic)
     * @param data Video data pointer
     * @param size Data size
     * @param caps GStreamer caps
     * @return Processed GstBuffer
     */
    GstBuffer* ProcessVideoDataChannelP(const guint8* data, gsize size, GstCaps* caps);

    /**
     * @brief Convert 10-bit packed data to 8-bit
     * @param input_data 10-bit packed data
     * @param input_size Input data size
     * @param output_buffer Output buffer for 8-bit data
     * @param output_size Output buffer size
     * @return Number of bytes written to output
     */
    gsize Convert10bitTo8bit(const guint8* input_data,
                             gsize         input_size,
                             guint8*       output_buffer,
                             gsize         output_size);

    // CDG0.0 format constants based on demuxer.c
    static constexpr gsize  kLineHeaderSize    = 16;    // MSF_LINE_HEADER_SIZE
    static constexpr gsize  kLineParamSize     = 32;    // MSF_LINE_PARAM_SIZE
    static constexpr gsize  kLineDataPadding   = 1280;  // MSF_LINE_DATA_PANDDING
    static constexpr guint  kDefaultImageWidth  = 4096;
    static constexpr guint  kDefaultImageHeight = 4096;
    static constexpr guint  kPixelDepth        = 10;
    static constexpr gint64 kDefaultStrideLines = 32;   // 16line一包完整参数

    // Member variables
    bool                     initialized_;
    gsize                    block_size_;
    gint64                   stride_offset_;
    std::vector<guint8>      conversion_buffer_;
    
    // Configurable properties
    CDG00Channel             channel_;          // 通道选择
    guint                    stride_lines_;     // 步长行数
    guint                    image_width_;      // 图像宽度
    guint                    image_height_;     // 图像高度
    
    // Helper methods to recalculate based on properties
    void RecalculateParameters();
};

}  // namespace msf

#endif  // MSF_CDG00_READER_HPP
