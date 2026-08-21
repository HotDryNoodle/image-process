/**
 * @file video.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 封装视频编解码接口
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
#include <iostream>

namespace lynsdk {
/**
 * @brief 视频解封装接口
 *
 */
class VideoDemuxer {
    std::string path;
    static void close(lynDemuxHandle_t &p) {
        if (p) {
            CHECK_ERR(lynDemuxClose(p));
            p = nullptr;
        }
    }
    CObject<lynDemuxHandle_t> demux_handle { nullptr, close };
    lynCodecPara_t codec_para;
    lynVdecOutInfo_t out_info;
    lynVdecAttr_t vdec_attr;

public:
    /**
     * @brief 构造函数
     *
     * @param path 视频地址, 可以是本地文件或者网络地址
     */
    VideoDemuxer(const std::string &path)
        : path(path) {
        CHECK_ERR(lynDemuxOpen(&demux_handle.obj, path.c_str(), nullptr));
        CHECK_ERR(lynDemuxGetCodecPara(demux_handle.obj, &codec_para));
    }
    lynCodecPara_t get_codec_para() const {
        return codec_para;
    }
    /**
     * @brief 视频解封装
     *
     * @return Packet
     */
    lynPacket_t demux() const {
        lynPacket_t pkt {}; //创建一个空编码包
        lynError_t err = lynDemuxReadPacket(demux_handle.obj, &pkt); //从解封装器中读取一个编码包
        if (err == lynEEOF) {
            pkt.eos = true;
        } else {
            CHECK_ERR(err);
        }
        return pkt;
    }
    static void free_packet(lynPacket_t p) {
        if (p.data) {
            CHECK_ERR(lynDemuxFreePacket(&p));
        }
    }
};

/**
 * @brief 视频解码参数
 *
 */
class VDecAttr {
    lynCodecPara_t codec_para;
    lynVdecOutInfo_t out_info;
    lynVdecAttr_t attr;
    void scale_to(size_t max_w, size_t max_h) {
        if (out_info.width > max_w || out_info.height > max_h) {
            auto scale_w = out_info.width / double(max_w);
            auto scale_h = out_info.height / double(max_h);
            double scale = std::max(scale_w, scale_h);
            if (scale > 4) {
                attr.scale = SCALE_DOWN_8X;
            } else if (scale > 2) {
                attr.scale = SCALE_DOWN_4X;
            } else {
                attr.scale = SCALE_DOWN_2X;
            }
            CHECK_ERR(lynVdecGetOutInfo(&codec_para, &attr, &out_info));
        }
    }

public:
    explicit VDecAttr(
        lynCodecPara_t para, lynPixelFormat_t output_fmt = LYN_PIX_FMT_NV12, lynScale_t scale = SCALE_NONE)
        : codec_para(para)
        , attr(lynVdecAttr_t { para.codecId, output_fmt, scale, false}) {
        CHECK_ERR(lynVdecGetOutInfo(&codec_para, &attr, &out_info));
    }
    /**
     * @brief Construct a new VDecAttr object
     *
     * @param para
     * @param max 自动设置scale到最大宽高
     * @param output_fmt
     */
    explicit VDecAttr(lynCodecPara_t para, const ImageSize &max, lynPixelFormat_t output_fmt = LYN_PIX_FMT_NV12)
        : codec_para(para)
        , attr(lynVdecAttr_t { para.codecId, output_fmt, SCALE_NONE, false }) {
        CHECK_ERR(lynVdecGetOutInfo(&codec_para, &attr, &out_info));
        scale_to(max.width, max.height);
    }

    VDecAttr(const VDecAttr &other) = default;
    VDecAttr &operator=(const VDecAttr &other) = default;
    VDecAttr() = default;

    const lynVdecAttr_t &get() const {
        return attr;
    }
    /**
     * @brief 获取解码器输出的图像类型
     *
     * @return ImageType
     */
    ImageType get_output_type() const {
        return ImageType {
            w : int32_t(out_info.width),
            h : int32_t(out_info.height),
            fmt : LYN_PIX_FMT_NV12,
            scale : attr.scale,
        };
    }
    /**
     * @brief 获取解码器输出一帧数据的字节大小
     *
     * @return size_t
     */
    size_t get_output_size() const {
        return out_info.predictBufSize;
    }
};

/**
 * @brief 视频解码接口封装
 *
 */
class VideoDecoder {
    std::string path;
    lynFrame_t frame = {};
    size_t out_size = 0;
    static void close(lynVdecHandle_t &p) {
        if (p) {
            CHECK_ERR(lynVdecClose(p));
            p = nullptr;
        }
    }
    CObject<lynVdecHandle_t> vdec_handle { nullptr, close };

public:
    /**
     * @brief Construct a new Video Decoder object
     *
     * @param attr
     */
    VideoDecoder(const VDecAttr attr) {
        CHECK_ERR(lynVdecOpen(&vdec_handle.obj, &attr.get())); //打开解码器
        frame.eos = false;
        out_size = attr.get_output_size();
        frame.size = attr.get_output_size();
    };
    /**
     * @brief 发送一个pkt到解码器
     *
     * @param send_stream
     * @param pkt
     */
    void send(const Stream &send_stream, const lynPacket_t &pkt) {
        CHECK_ERR(lynVdecSendPacketAsync(send_stream.get(), vdec_handle.obj, &pkt));
    };
    /**
     * @brief 从解码器接收一帧
     *
     * @param s
     * @param img
     */
    void recv(const Stream &s, const LynData &img) {
        if (img.get_size() < out_size) {
            throw std::invalid_argument("data size is too small");
        }
        frame.data = img.pointer();
        CHECK_ERR(lynVdecRecvFrameAsync(s.get(), vdec_handle.obj, &frame));
    }
    void decode(const Stream &send_stream, const Stream &recv_stream, const lynPacket_t &pkt, const LynData &img) {
        send(send_stream, pkt);
        recv(recv_stream, img);
    }
};

/**
 * @brief 解封装和解码视频
 *
 */
class VideoReader {
    VideoDemuxer demuxer;
    VDecAttr attr;
    VideoDecoder decoder;

public:
    VideoReader(const std::string &path, lynPixelFormat_t output_fmt = LYN_PIX_FMT_NV12, lynScale_t scale = SCALE_NONE)
        : demuxer(path)
        , attr(demuxer.get_codec_para(), output_fmt, scale)
        , decoder(attr) {
    }
    VideoReader(const std::string &path, const ImageSize &max, lynPixelFormat_t output_fmt = LYN_PIX_FMT_NV12)
        : demuxer(path)
        , attr(demuxer.get_codec_para(), max, output_fmt)
        , decoder(attr) {
    }
    const VideoDemuxer &get_demuxer() const {
        return demuxer;
    }
    const VideoDecoder &get_decoder() const {
        return decoder;
    }
    VideoDemuxer &get_demuxer() {
        return demuxer;
    }
    VideoDecoder &get_decoder() {
        return decoder;
    }
    bool read(const Stream &send_stream, const Stream &recv_stream, const LynData &img) {
        auto pkt = demuxer.demux();
        decoder.decode(send_stream, recv_stream, pkt, img);
        send_stream.add_callback([pkt]() { VideoDemuxer::free_packet(pkt); });
        return pkt.eos;
    }
    const VDecAttr &get_attr() const {
        return attr;
    }
};

/**
 * @brief 视频编码参数
 *
 */
struct VEncAttr : public lynVencAttr_t {
    uint32_t frame_size = 0;
    VEncAttr() {
        CHECK_ERR(lynVencSetDefaultParams(this));
    }
    void set_frame(uint32_t frame_size, uint32_t width, uint32_t height, lynPixelFormat_t input_fmt) {
        this->frame_size = frame_size;
        this->width = width;
        this->height = height;
        this->inputFormat = input_fmt;
    }
    void set_video(lynCodecId_t codec_type, float fps) {
        this->codecType = codec_type;
        this->fps = fps;
    }
    lynVencAttr_t as_raw() {
        return *this;
    }
};

/**
 * @brief 视频编码接口封装
 *
 */
class VideoEncoder {
public:
private:
    lynFrame_t frame = {}; // 编码前数据
    lynPacket_t pkt = {}; // 编码后数据
    VEncAttr attr;
    static void close(lynVencHandle_t &p) {
        if (p) {
            CHECK_ERR(lynVencClose(p));
            p = nullptr;
        }
    }
    CObject<lynVencHandle_t> venc_handle { nullptr, close };

public:
    VideoEncoder(const VEncAttr &attr)
        : attr(attr) {
        CHECK_ERR(lynVencOpen(&venc_handle.obj, &attr));
        frame.size = attr.frame_size;
        frame.eos = false;
        pkt.size = frame.size;
    }
    /**
     * @brief 从编码器接收一个pkt
     *
     * @param recv_stream
     * @param out
     */
    void recv(const Stream &recv_stream, const LynData &out) {
        pkt.data = out.pointer();
        CHECK_ERR(lynVencRecvPacketAsync(recv_stream.get(), venc_handle.obj, &pkt));
    }
    /**
     * @brief 发送一帧数据到编码器
     *
     * @param send_stream
     * @param in
     * @param eos
     */
    void send(const Stream &send_stream, const LynData &in, bool eos) {
        if (in.get_size() < attr.frame_size) {
            throw std::invalid_argument("data size is too small");
        }
        frame.data = const_cast<uint8_t *>(in.pointer());
        frame.eos = eos;
        CHECK_ERR(lynVencSendFrameAsync(send_stream.get(), venc_handle.obj, &frame));
    };
    void encode(
        const Stream &send_stream, const Stream &recv_stream, const LynData &in, bool eos, const LynData &out,
        const std::function<void(CPUData pkt)> &cb) {
        send(send_stream, in, eos);
        recv(recv_stream, out);
        recv_stream.add_callback([out, cb]() {
            auto pkt = out.read_pkt();
            cb(pkt);
        });
    }
    void encode(const Stream &send_stream, const Stream &recv_stream, const LynData &in, bool eos, const LynData &out) {
        send(send_stream, in, eos);
        recv(recv_stream, out);
    }
    size_t get_input_size() const {
        return frame.size;
    }
    size_t get_output_size() const {
        return pkt.size;
    }
    const VEncAttr &get_attr() const {
        return attr;
    }
};
} // namespace lynsdk