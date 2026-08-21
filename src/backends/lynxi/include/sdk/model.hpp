/**
 * @file model.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 提供Model类封装model接口
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
#include <vector>

namespace lynsdk {
/**
 * @brief 用于打印的apu tensor数据类型名
 *
 */
const std::array<const char *, 16> DTYPE_NAMES
    = { "DT_FLOAT",  "DT_FLOAT16", "DT_INT8", "DT_INT16",  "DT_UINT16", "DT_UINT8", "DT_INT32", "DT_INT64",
        "DT_UINT32", "DT_UINT64",  "DT_BOOL", "DT_DOUBLE", "DT_BF16",   "DT_TF32",  "DT_INT4",  "DT_UNDEFINED" };

/**
 * @brief 封装model接口
 *
 */
class Model {
public:
    /**
     * @brief 模型的tensor属性
     *
     */
    struct TensorAttr {
        uint32_t batch_size; // batch大小
        uint32_t data_len; // 数据长度
        uint32_t data_size; // 数据字节大小
        lynDataType_t data_type; // 数据类型
        std::vector<uint32_t> dims; // 维度
        std::string tensor_name; // 名称

        TensorAttr(const lynModelTensorAttr_t *attr) {
            batch_size = attr->batchSize;
            data_len = attr->dataNum;
            data_size = attr->dataLen;
            data_type = attr->dtype;
            dims = std::vector<uint32_t>(attr->dims, attr->dims + attr->dimCount);
            tensor_name = std::string(attr->tensorName);
        }
    };

    /**
     * @brief 模型输入输出属性
     *
     */
    struct Desc {
        uint64_t input_data_size = 0; // 单batch输入数据字节大小
        uint64_t output_data_size = 0; // 单batch输出数据字节大小
        std::vector<TensorAttr> input; // 输入属性
        std::vector<TensorAttr> output; // 输出属性

        Desc() = default;

        Desc(lynModelDesc_t *raw) {
            input_data_size = raw->inputDataLen;
            output_data_size = raw->outputDataLen;
            input.reserve(raw->inputTensorAttrArrayNum);
            output.reserve(raw->outputTensorAttrArrayNum);
            for (uint32_t i = 0; i < raw->inputTensorAttrArrayNum; i++) {
                input.emplace_back(&raw->inputTensorAttrArray[i]);
            }
            for (uint32_t i = 0; i < raw->outputTensorAttrArrayNum; i++) {
                output.emplace_back(&raw->outputTensorAttrArray[i]);
            }
        }
    };

private:
    static void close(lynModel_t &p) {
        if (p) {
            CHECK_ERR(lynUnloadModel(p));
            p = nullptr;
        }
    }
    CObject<lynModel_t> model { nullptr, close };
    lynModelDesc_t *raw_model_desc = nullptr;
    Desc model_desc;
    uint32_t batch_size = 0;

public:
    /**
     * @brief 调用lynLoadModel和lynModelGetDesc构造Model对象
     *
     * @param model_path 包含top_graph.json的模型文件路径,如 /path/to/model/Net_0
     */
    Model(const std::string &model_path) {
        CHECK_ERR(lynLoadModel(model_path.c_str(), &model.obj)); //加载模型
        CHECK_ERR(lynModelGetDesc(model.obj, &raw_model_desc));
        model_desc = Desc(raw_model_desc);
        batch_size = model_desc.input[0].batch_size;
    }
    /**
     * @brief 从DTYPE_NAMES获取dtype的字符串表示
     *
     * @param dtype
     * @return const char*
     */
    static const char *get_dtype_name(lynDataType_t dtype) noexcept {
        return DTYPE_NAMES.at(dtype);
    }
    /**
     * @brief 单batch输入数据字节大小
     *
     * @return size_t
     */
    size_t get_inputs_size() const {
        return model_desc.input_data_size;
    }
    /**
     * @brief 单batch输出数据字节大小
     *
     * @return size_t
     */
    size_t get_outputs_size() const {
        return model_desc.output_data_size;
    }
    /**
     * @brief 第一个输入tensor的batch_size
     *
     * @return size_t
     */
    size_t get_batch_size() const {
        return batch_size;
    }
    /**
     * @brief 执行模型预测
     *
     * @param s
     * @param in
     * @param out
     */
    void predict(const Stream &s, const LynData &in, const LynData &out) const {
        CHECK_ERR(lynExecuteModelAsync(s.get(), model.obj, (void *)(in.pointer()), out.pointer(), batch_size));
    }
    /**
     * @brief 执行模型预测，异步apu接口模式
     *
     * @param send
     * @param recv
     * @param in
     * @param out
     */
    void predict(const Stream &send, const Stream &recv, const LynData &in, const LynData &out) const {
        CHECK_ERR(lynModelSendInputAsync(send.get(), model.obj, (void *)(in.pointer()), out.pointer(), batch_size));
        CHECK_ERR(lynModelRecvOutputAsync(recv.get(), model.obj));
    }
    /**
     * @brief 动态batch，使用异步apu接口
     *
     * @param send
     * @param recv
     * @param in
     * @param out
     */
    void predict_batch(const Stream &send, const Stream &recv, const LynData &in, const LynData &out) const {
        size_t start = 0;
        for (size_t i = 0; i < in.get_batch_size(); i += batch_size) {
            predict(send, recv, in.slice(start, i), out.slice(start, i));
            start = i;
        }
        if (start != batch_size) {
            predict(send, recv, in.slice(start, batch_size), out.slice(start, batch_size));
        }
    }
    /**
     * @brief 动态batch，使用同步apu接口
     *
     * @param s
     * @param in
     * @param out
     */
    void predict_batch(const Stream &s, const LynData &in, const LynData &out) const {
        size_t start = 0;
        for (size_t i = 0; i < in.get_batch_size(); i += batch_size) {
            predict(s, in.slice(start, i), out.slice(start, i));
            start = i;
        }
        if (start != batch_size) {
            predict(s, in.slice(start, batch_size), out.slice(start, batch_size));
        }
    }
    void send(const Stream &send, /*const Stream &recv,*/ const LynData &in, const LynData &out) const {
        CHECK_ERR(lynModelSendInputAsync(send.get(), model.obj, (void *)(in.pointer()), out.pointer(), batch_size));
    }
    void recv(const Stream &recv) const {
        CHECK_ERR(lynModelRecvOutputAsync(recv.get(), model.obj));
    }
    /**
     * @brief 获取模型属性原始类型
     *
     * @return const lynModelDesc_t*
     */
    const lynModelDesc_t *get_raw_model_desc() const {
        return raw_model_desc;
    }
    /**
     * @brief 获取模型属性
     *
     * @return const Desc&
     */
    const Desc &get_model_desc() const {
        return model_desc;
    }
};
} // namespace lynsdk