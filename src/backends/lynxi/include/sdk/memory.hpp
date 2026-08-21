/**
 * @file memory.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 内存拷贝接口封装
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "../utils/cpu_data.hpp"
#include "error.hpp"
#include "stream.hpp"

namespace lynsdk {
/**
 * @brief 内存拷贝接口封装
 *
 */
class LynData : public DataPointer {
    LynData(DataPointer &&dp)
        : DataPointer(std::move(dp)) {
    }

    void check() const {
        if (empty()) {
            throw std::invalid_argument("self is empty");
        }
    }

    void check(size_t target_size) const {
        check();
        if (target_size > size) {
            throw std::out_of_range("data size is too large");
        }
    }

    void check(const CPUData &data) const {
        check(data.get_size());
    }

    void check(const LynData &data) const {
        check(data.get_size());
    }

public:
    LynData() = default;
    LynData(size_t size_per_batch, size_t batch = 1)
        : DataPointer(
            std::shared_ptr<uint8_t>(
                [size = size_per_batch * batch]() -> uint8_t * {
                    void *p = nullptr;
                    CHECK_ERR(lynMalloc(&p, size));
                    return static_cast<uint8_t *>(p);
                }(),
                [](uint8_t *p) {
                    CObject<uint8_t *> _p { p, [](uint8_t *&p) {
                                               CHECK_ERR(lynFree(p));
                                               p = nullptr;
                                           } };
                }),
            batch, size_per_batch) {};
    LynData slice(size_t batch_start, size_t batch_end) const {
        return LynData(DataPointer::slice(batch_start, batch_end));
    }
    /**
     * @brief 异步写入主机上数据
     *
     * @param s
     * @param data
     */
    void write(const Stream &s, const CPUData &data) {
        check(data);
        CHECK_ERR(lynMemcpyAsync(s.get(), pointer(), data.pointer(), data.get_size(), ClientToServer));
    }
    /**
     * @brief 异步读取数据到主机上
     *
     * @param s
     * @param data
     */
    void read(const Stream &s, CPUData &data) const {
        check(data);
        CHECK_ERR(lynMemcpyAsync(s.get(), data.pointer(), pointer(), data.get_size(), ServerToClient));
    };
    /**
     * @brief 写入主机上数据
     *
     * @param data
     */
    void write(const CPUData &data) {
        check(data);
        CHECK_ERR(lynMemcpy(pointer(), data.pointer(), data.get_size(), ClientToServer));
    }
    void copy_from(const LynData &data) {
        check(data);
        CHECK_ERR(lynMemcpy(pointer(), data.pointer(), data.get_size(), ServerToServer));
    }
    void copy_from(const Stream &s, const LynData &data) {
        check(data);
        CHECK_ERR(lynMemcpyAsync(s.get(), pointer(), data.pointer(), data.get_size(), ServerToServer));
    }
    void mem_set(const Stream &s, int32_t n) {
        check();
        CHECK_ERR(lynMemsetAsync(s.get(), pointer(), n, size));
    }
    void mem_set(int32_t n) {
        check();
        CHECK_ERR(lynMemset(pointer(), n, size));
    }
    /**
     * @brief 读取数据到主机上
     *
     * @param data
     */
    void read(CPUData &data) const {
        check(data);
        CHECK_ERR(lynMemcpy(data.pointer(), pointer(), data.get_size(), ServerToClient));
    };
    CPUData read(const Stream &s) const {
        CPUData data(size_per_batch, batch);
        read(s, data);
        return data;
    }
    CPUData read() const {
        CPUData data(size_per_batch, batch);
        read(data);
        return data;
    }
    /**
     * @brief 获取pkt的真实大小
     *
     * @return size_t
     */
    size_t get_pkt_size() const {
        lynCodecBuf_t p { pointer(), uint32_t(get_size()), false, nullptr, nullptr};
        uint32_t valid_size;
        CHECK_ERR(lynEncGetRemotePacketValidSize(&p, &valid_size));
        return valid_size;
    }
    /**
     * @brief 读取pkt到主机
     *
     * @return CPUData
     */
    CPUData read_pkt() const {
        auto size = get_pkt_size();
        CPUData ret(size);
        read(ret);
        return ret;
    }
    LynData slice_in_batch(size_t start, size_t end, size_t batch_index = 0) const {
        return LynData(DataPointer::slice_in_batch(start, end, batch_index));
    }
};

struct ImageSize {
    size_t width;
    size_t height;
    ImageSize(size_t width, size_t height)
        : width(width)
        , height(height) {
    }
};

/**
 * @brief 图像类型
 *
 */
struct ImageType {
    int32_t w = 0, h = 0;
    lynPixelFormat_t fmt;
    lynScale_t scale = SCALE_NONE;

    bool operator==(const ImageType &other) const {
        return w == other.w && h == other.h && fmt == other.fmt && scale == other.scale;
    }
};

/**
 * @brief 图像类型和数据
 *
 * @tparam DType
 */
template <typename DType>
struct Image;

/**
 * @brief 图像类型和芯片上的数据
 *
 */
template <>
struct Image<LynData> : public ImageType {
    Image() = default;
    Image(ImageType typ, LynData data)
        : ImageType(typ)
        , data(data) {
    }
    LynData data;
};

/**
 * @brief 图像类型和cpu上的数据
 *
 */
template <>
struct Image<CPUData> : public ImageType {
    Image() = default;
    Image(ImageType typ, CPUData data)
        : ImageType(typ)
        , data(data) {
    }
    CPUData data;
};
} // namespace lynsdk