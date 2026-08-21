/**
 * @file context.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 提供Context封装context接口
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "error.hpp"
#include <functional>
#include <memory>

namespace lynsdk {
/**
 * @brief 设备的context，context与线程绑定，拥有context的线程才能使用该设备
 *
 */
class Context {
public:
    /**
     * @brief Construct a new Context object
     *
     * @param id 设备id
     */
    Context(int id = 0) {
        CHECK_ERR(lynCreateContext(&p.obj, id));
    }
    using StreamErrorHandler = std::function<void(lynStream_t stream, ErrorMsg &&msg)>;
    /**
     * @brief 当stream中产生错误时的回调函数
     *
     * @param cb
     */
    void on_stream_error(StreamErrorHandler cb) {
        handler = std::make_unique<StreamErrorHandler>(cb);
        CHECK_ERR(lynRegisterErrorHandler(
            [](lynStream_t stream, lynErrorMsg_t *errorMsg, void *_ud) {
                auto ud = static_cast<StreamErrorHandler *>(_ud);
                (*ud)(stream, ErrorMsg { errorMsg });
            },
            handler.get()));
    }
    /**
     * @brief 设置当前线程的context
     *
     */
    void set_current() const {
        CHECK_ERR(lynSetCurrentContext(p.obj));
    }

private:
    static void close(lynContext_t &p) {
        if (p) {
            CHECK_ERR(lynDestroyContext(p));
            p = nullptr;
        }
    }
    CObject<lynContext_t> p { nullptr, close };
    std::unique_ptr<StreamErrorHandler> handler = nullptr;
};

/**
 * @brief 自动在析构、get时设置当前线程的context
 *
 * @tparam T
 */
template <typename T, typename... Args>
class AutoContext {
    Context ctx;
    T t;

public:
    AutoContext(int id, Args &&...args)
        : ctx(id)
        , t(std::forward<Args>(args)...) {
    }
    T &get() {
        ctx.set_current();
        return t;
    }
    const T &get() const {
        ctx.set_current();
        return t;
    }
    ~AutoContext() {
        ctx.set_current();
    }
};

/**
 * @brief 构造Context，然后构造T，返回AutoContext
 *
 * @tparam T
 * @tparam Args 构造T的参数类型
 * @param id 构造Context的参数
 * @param args 构造T的参数
 * @return AutoContext<T>
 */
template <typename T, typename... Args>
std::unique_ptr<AutoContext<T, Args...>> new_auto_context(int id, Args &&...args) {
    return std::make_unique<AutoContext<T, Args...>>(id, std::forward<Args>(args)...);
}
} // namespace lynsdk