/**
 * @file image.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 图像编解码接口封装和增强
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "error.hpp"
#include "memory.hpp"
#include "stream.hpp"
#include <fstream>
#include <iostream>

/**
 * @brief 从磁盘读取图片
 *
 */
namespace lynsdk {
/**
 * @brief 从磁盘读取图片,并更具图像宽高计算出合适的scale
 *
 * @exception std::invalid_argument 打开图片文件失败
 *
 * @param img_path 图片文件路径
 * @return CPUData 图片数据
 */
inline CPUData image_read(const std::string &img_path) {
    std::ifstream img_file(img_path, std::ios::binary | std::ios::in | std::ios::ate);
    if (!img_file.is_open()) {
        throw std::invalid_argument("open file failed: " + img_path);
    }
    auto size = img_file.tellg(); //获得图片文件大小
    img_file.seekg(0, std::ios::beg); //定位到文件头
    CPUData jpeg(size);
    img_file.read((char *)(jpeg.pointer()), size); //按字节读取文件
    return jpeg;
}

/**
 * @brief 图片解码参数
 *
 */
class IDecAttr {
    std::string img_url;
    lynImageDecPara_t para;
    lynImageInfo_t image_info;
    bool support;
    static void get_image_info(const lynImageDecPara_t &para, lynImageInfo_t &image_info, bool &support) {
        CHECK_ERR(lynImageGetInfo(&para, &image_info, &support));
    }
    void scale_to(size_t max_w, size_t max_h) {
        if (image_info.output.width > max_w || image_info.output.height > max_h) {
            auto scale_w = image_info.output.width / double(max_w);
            auto scale_h = image_info.output.height / double(max_h);
            double scale = std::max(scale_w, scale_h);
            if (scale > 4) {
                para.scale = SCALE_DOWN_8X;
            } else if (scale > 2) {
                para.scale = SCALE_DOWN_4X;
            } else {
                para.scale = SCALE_DOWN_2X;
            }
            CHECK_ERR(lynImageGetInfo(&para, &image_info, &support));
        }
    }

public:
    explicit IDecAttr(
        std::string name, lynPixelFormat_t output_fmt = LYN_PIX_FMT_NV12, lynScale_t scale = SCALE_NONE,
        bool align = true)
        : img_url(name)
        , para(lynImageDecPara_t { img_url.c_str(), scale, output_fmt, align }) {
        get_image_info(para, image_info, support);
    }
    /**
     * @brief Construct a new IDecAttr object
     *
     * @param name
     * @param max 自动设置scale到最大宽高
     * @param output_fmt
     * @param align
     */
    explicit IDecAttr(
        std::string name, const ImageSize &max, lynPixelFormat_t output_fmt = LYN_PIX_FMT_NV12, bool align = true)
        : img_url(name)
        , para(lynImageDecPara_t { img_url.c_str(), SCALE_NONE, output_fmt, align }) {
        get_image_info(para, image_info, support);
        scale_to(max.width, max.height);
    }

    IDecAttr(const IDecAttr &other) = default;
    IDecAttr &operator=(const IDecAttr &other) = default;
    IDecAttr() = default;

    /**
     * @brief 是否支持硬解码
     *
     * @return true
     * @return false
     */
    bool is_support() const {
        return support;
    }
    const lynImageInfo_t &get_image_info() const {
        return image_info;
    }
    ImageType get_output_type() const {
        ImageType out;
        out.w = image_info.output.width;
        out.h = image_info.output.height;
        out.fmt = image_info.output.outputFmt;
        out.scale = image_info.output.scale;
        return out;
    }
    lynJdecAttr_t get() const {
        return lynJdecAttr_t { image_info.output.outputFmt, image_info.output.scale };
    }
    size_t get_output_size() const {
        return image_info.output.predictBufSize;
    }
    const std::string &get_image_url() const {
        return img_url;
    }
};

/**
 * @brief 图片软解码
 *
 * @param jpeg
 * @param image_info
 * @return CPUData
 */
inline CPUData image_decode_soft(const CPUData &jpeg, const lynImageInfo_t &image_info) {
    lynImageDecAttr_t attr;
    attr.info = image_info;
    attr.inBuf.size = jpeg.get_size();
    attr.inBuf.data = const_cast<uint8_t *>(jpeg.pointer());
    attr.outBuf.size = image_info.output.predictBufSize;
    CPUData out(image_info.output.predictBufSize);
    attr.outBuf.data = out.pointer();
    CHECK_ERR(lynImageDecodeSoft(&attr));
    return out;
}

/**
 * @brief jpeg硬解码
 *
 * @param stream
 * @param in
 * @param out
 * @param image_info
 */
inline void jpeg_decode(const Stream &stream, const LynData &in, const LynData &out, const lynImageInfo_t &image_info) {
    lynImageDecAttr_t attr {};
    attr.info = image_info;
    attr.inBuf = lynCodecBuf_t { in.pointer(), uint32_t(in.get_size()), false, nullptr, nullptr};
    attr.outBuf = lynCodecBuf_t { out.pointer(), image_info.output.predictBufSize, false, nullptr, nullptr};
    CHECK_ERR(lynJpegDecodeAsync(stream.get(), &attr));
    return;
}

/**
 * @brief 异步图片硬解码
 *
 */
class ImageDecoder {
    static void close(lynJdecHandle_t &p) {
        if (p) {
            CHECK_ERR(lynJdecClose(p));
            p = nullptr;
        }
    }
    CObject<lynJdecHandle_t> jdecHdl { nullptr, close };
    lynJdecAttr_t attr;

public:
    ImageDecoder(lynJdecAttr_t attr)
        : attr(std::move(attr)) {
        CHECK_ERR(lynJdecOpen(&jdecHdl.obj, &attr));
    };
    void send(const Stream &s, CPUData &data) {
        lynPacket_t pkt = {};
        pkt.data = data.pointer();
        pkt.size = data.get_size();
        CHECK_ERR(lynJdecSendPacketAsync(s.get(), jdecHdl.obj, &pkt));
    }
    void recv(const Stream &s, const LynData &data) {
        lynFrame_t frame {};
        frame.data = data.pointer();
        frame.size = data.get_size();
        CHECK_ERR(lynJdecRecvFrameAsync(s.get(), jdecHdl.obj, &frame));
    }
    void decode(const Stream &send_stream, const Stream &recv_stream, CPUData &in, const LynData &out) {
        send(send_stream, in);
        recv(recv_stream, out);
    }
};

/**
 * @brief jpeg硬编码
 *
 * @param stream
 * @param in
 * @param out
 * @param img_type
 * @param cb
 * @param quality
 */
inline void jpeg_encode(
    const Stream &stream, const LynData &in, const LynData &out, const ImageType &img_type,
    const std::function<void(CPUData pkt)> &cb, uint32_t quality = 95) {
    lynJencInfo_t info {};
    info.params = lynJencAttr_t { img_type.fmt, uint32_t(img_type.w), uint32_t(img_type.h), quality };
    info.input = lynCodecBuf_t { in.pointer(), uint32_t(img_type.h * img_type.w * 3 / 2), false, nullptr, nullptr};
    info.output = lynCodecBuf_t { out.pointer(), uint32_t(out.get_size()), false, nullptr, nullptr};
    CHECK_ERR(lynJpegEncodeAsync(stream.get(), &info));
    stream.add_callback([out, cb] {
        // uint32_t valid_size;
        auto pkt = out.read_pkt();
        cb(pkt);
    });
    return;
}

/**
 * @brief jpeg硬编码
 *
 * @param stream
 * @param in
 * @param out
 * @param img_type
 * @param quality
 */
inline void jpeg_encode(
    const Stream &stream, const LynData &in, const LynData &out, const ImageType &img_type, uint32_t quality = 95) {
    lynJencInfo_t info {};
    info.params = lynJencAttr_t { img_type.fmt, uint32_t(img_type.w), uint32_t(img_type.h), quality };
    info.input = lynCodecBuf_t { in.pointer(), uint32_t(img_type.h * img_type.w * 3 / 2), false, nullptr, nullptr};
    info.output = lynCodecBuf_t { out.pointer(), uint32_t(out.get_size()), false, nullptr, nullptr};
    CHECK_ERR(lynJpegEncodeAsync(stream.get(), &info));
    return;
}

/**
 * @brief 异步图像硬编码接口
 *
 */
class ImageEncoder {
    lynJencAttr_t attr;
    static void close(lynJencHandle_t &handle) {
        if (handle) {
            CHECK_ERR(lynJencClose(handle));
            handle = nullptr;
        }
    }
    CObject<lynJencHandle_t> handle { nullptr, close };
    size_t in_size;

public:
    ImageEncoder(const ImageType &img_type, uint32_t quality = 95)
        : attr(lynJencAttr_t { img_type.fmt, uint32_t(img_type.w), uint32_t(img_type.h), quality }) {
        CHECK_ERR(lynJencOpen(&handle.obj, &attr));
        in_size = attr.height * attr.width * 3 / 2;
    }
    void recv(const Stream &s, const LynData &data) {
        lynPacket_t pkt = {};
        pkt.data = data.pointer();
        pkt.size = data.get_size();
        CHECK_ERR(lynJencRecvPacketAsync(s.get(), handle.obj, &pkt));
    }
    void send(const Stream &s, const LynData &data) {
        lynFrame_t frame {};
        frame.data = data.pointer();
        frame.size = in_size;
        CHECK_ERR(lynJencSendFrameAsync(s.get(), handle.obj, &frame));
    }
    void encode(
        const Stream &send_stream, const Stream &recv_stream, const LynData &in, const LynData &out,
        const std::function<void(CPUData pkt)> &cb) {
        send(send_stream, in);
        recv(recv_stream, out);
        recv_stream.add_callback([out, cb]() {
            auto pkt = out.read_pkt();
            cb(pkt);
        });
    }
};
} // namespace lynsdk