/**
 * @file cpu_data.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 提供CPUData
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include <cstring>
#include <exception>
#include <malloc.h>
#include <memory>
#include <type_traits>

namespace lynsdk {
using Float16 = int16_t;

/**
 * @brief 提供一组方法操作字节数据的方法
 *
 */
class DataPointer {
protected:
    std::shared_ptr<uint8_t> p = nullptr;
    size_t size = 0;
    size_t batch = 0;
    size_t size_per_batch = 0;
    size_t offset = 0;

public:
    DataPointer() = default;
    /**
     * @brief 构造一个DataPointer对象
     *
     * @exception std::invalid_argument 如果size % batch != 0，则抛出异常
     *
     * @param p
     * @param size_per_batch
     * @param batch
     * @param offset
     */
    DataPointer(std::shared_ptr<uint8_t> p, size_t batch, size_t size_per_batch, size_t offset = 0)
        : p(move(p))
        , batch(batch)
        , size_per_batch(size_per_batch)
        , offset(offset) {
        size = size_per_batch * batch;
    };
    /**
     * @brief 获取切片
     *
     * @exception std::out_of_range 如果batch_start或batch_end超出范围，则抛出异常
     *
     * @param batch_start
     * @param batch_end
     * @return DataPointer
     */
    DataPointer slice(size_t batch_start, size_t batch_end) const {
        if ( batch_start > batch_end || batch_end > batch) {
            throw std::out_of_range("batch out of range");
        }
        auto slice_batch = batch_end - batch_start;
        return DataPointer(p, size_per_batch, slice_batch, batch_start * size_per_batch);
    }
    DataPointer slice_in_batch(size_t start, size_t end, size_t batch_index = 0) const {
        if ( start > end || end > size_per_batch || batch_index > batch) {
            throw std::out_of_range("batch out of range");
        }
        return DataPointer(p, end - start, 1, batch_index * size_per_batch + start);
    }
    /**
     * @brief 获取指定位置数据的const指针
     *
     * @exception std::out_of_range 如果batch_index或index超出范围，则抛出异常
     *
     * @tparam DType
     * @param batch_index
     * @param index
     * @return const DType*
     */
    template <typename DType = uint8_t>
    DType *pointer(size_t batch_index = 0, size_t index = 0) const {
        index = index * sizeof(DType);
        if (batch_index >= batch || index >= size_per_batch) {
            throw std::out_of_range("batch index out of range: " + std::to_string(batch_index) + 
                               ", index out of range: " + std::to_string(index) +
                               ", batch limit: " + std::to_string(batch) + 
                               ", size_per_batch limit: " + std::to_string(size_per_batch));
        }
        return (DType *)(p.get() + offset + batch_index * size_per_batch + index);
    }
    virtual ~DataPointer() {
        p = nullptr;
    };
    /**
     * @brief 获取数据字节大小
     *
     * @tparam DType
     * @return size_t
     */
    template <typename DType = uint8_t>
    size_t get_size() const {
        return size / sizeof(DType);
    }
    /**
     * @brief 获取每batch数据字节大小
     *
     * @tparam DType
     * @return size_t
     */
    template <typename DType = uint8_t>
    size_t get_size_per_batch() const {
        return size_per_batch / sizeof(DType);
    }
    /**
     * @brief 获取batch数量
     *
     * @return size_t
     */
    size_t get_batch_size() const {
        return batch;
    }
    /**
     * @brief 判断size是否为0
     *
     * @return bool
     */
    bool empty() const {
        return size == 0;
    }
};

/**
 * @brief 提供一组方法操作cpu字节数据的方法
 *
 */
class CPUData : public DataPointer {
    CPUData(DataPointer &&dp)
        : DataPointer(std::move(dp)) {
    }

    static float half2float(int16_t ib) {
        const int sig[2] = { 1, -1 };
        const float result = 5.96046e-08;

        int16_t s, e, m;
        s = (ib >> 15) & 0x1;
        e = (ib >> 10) & 0x1f;
        m = ib & 0x3ff;

        // added by puyang.wang@lynxi.com
        {
            if (0 == e)
                return sig[s] * m * result;
            else {
                union {
                    unsigned int u32;
                    float f32;
                } ou;

                e = (0x1f == e) ? 0xff : (e - 15 + 127);
                ou.u32 = (s << 31) | (e << 23) | (m << 13);
                return ou.f32;
            }
        }
    }

public:
    CPUData() = default;
    /**
     * @brief 创建指定大小的数据，并设置batch
     *
     * @param size_per_batch 每batch数据总大小
     * @param batch 数据批次个数，每个批次大小为size/batch
     */
    CPUData(size_t size_per_batch, size_t batch = 1)
        : DataPointer(
#if defined(__arm__) || defined(__aarch64__)
            std::shared_ptr<uint8_t>(
                static_cast<uint8_t *>(pvalloc(size_per_batch * batch)), [](uint8_t *p) { free(p); })
#else
            std::shared_ptr<uint8_t>(new uint8_t[size_per_batch * batch], [](uint8_t *p) { delete[] p; })
#endif
                ,
            batch, size_per_batch) {};
    /**
     * @brief 按batch切片一个新的CPUData
     *
     * @param batch_start
     * @param batch_end
     * @return CPUData
     */
    CPUData slice(size_t batch_start, size_t batch_end) const {
        return CPUData(DataPointer::slice(batch_start, batch_end));
    }
    /**
     * @brief 从已存在的数据构造一个的CPUData
     *
     * @param data
     * @param size_per_batch
     * @param batch
     */
    CPUData(std::shared_ptr<uint8_t> data, size_t size_per_batch, size_t batch = 1)
        : DataPointer(data, size_per_batch, batch) {
    }
    /**
     * @brief 以DType解析数据,获取指定位置的值
     *
     * @tparam DType
     * @param batch_index
     * @param index
     * @return DType
     */
    template <typename DType = uint8_t>
    std::enable_if_t<!std::is_same<DType, Float16>::value, DType> at(size_t batch_index, size_t index) const {
        return *pointer<DType>(batch_index, index);
    };

    template <typename DType>
    std::enable_if_t<std::is_same<DType, Float16>::value, float> at(size_t batch_index, size_t index) const {
        auto ib = *pointer<int16_t>(batch_index, index);
        return half2float(ib);
    };

    /**
     * @brief 从其它CPUData拷贝数据,拷贝数据的大小为本身大小和data大小的最小值
     *
     * @param data
     */
    void write(CPUData data) {
        memcpy(p.get(), data.pointer(), get_size() < data.get_size() ? get_size() : data.get_size());
    }
    CPUData fp16tofp32() const {
        auto fp32 = CPUData(get_size_per_batch() * 2, get_batch_size());
        for (size_t i = 0; i < get_batch_size(); i++) {
            for (size_t j = 0; j < get_size_per_batch<int16_t>(); j++) {
                *fp32.pointer<float>(i, j) = at<Float16>(i, j);
            }
        }
        return fp32;
    }
    CPUData slice_in_batch(size_t start, size_t end, size_t batch_index = 0) const {
        return CPUData(DataPointer::slice_in_batch(start, end, batch_index));
    }
};
} // namespace lynsdk
