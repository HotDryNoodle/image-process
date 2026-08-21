/**
 * @file error.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 错误处理
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include <exception>
#include <functional>
#include <lyn_api.h>
#include <sstream>
#include <string>

namespace lynsdk {
struct ErrorMsg {
    lynError_t err_code = 0;
    std::string err_msg;
    std::string err_module;
    std::string err_function;
    ErrorMsg(lynErrorMsg_t *errorMsg) {
        if (errorMsg) {
            err_code = errorMsg->errCode;
            err_msg = errorMsg->errMsg == nullptr ? "" : errorMsg->errMsg;
            err_module = errorMsg->errModule == nullptr ? "" : errorMsg->errModule;
            err_function = errorMsg->errFunction == nullptr ? "" : errorMsg->errFunction;
        }
    }
};

/**
 * @brief 对sdk错误码的封装
 *
 */
class Error : public std::exception {
private:
    lynError_t err = 0; // SDK 错误号
    std::string file;
    int line;
    std::string detail; // 详细信息

    std::string s;
    void String() {
        std::stringstream ss;
        ss << "[" << file << ":" << line << "]";
        ss << " code: " << err;
        if (!detail.empty()) {
            ss << " detail: " << detail;
        }
        ss << std::endl;
        s = ss.str();
    }

public:
    Error(lynError_t err, const std::string &file, int line, const std::string &detail = "")
        : err(err)
        , file(file)
        , line(line)
        , detail(detail) {
        String();
    }
    Error& operator=(const Error& e) noexcept {
        if (this != &e) {
            err = e.err;
            file = e.file;
            line = e.line;
            detail = e.detail;
            s = e.s;
        }
        return *this;
    }
    Error(const Error &e) noexcept {
        *this = e;
    };
    const char *what() const noexcept override {
        return s.c_str();
    }
    lynError_t code() const noexcept {
        return err;
    }
};

/**
 * @brief 检测sdk 接口返回值，非0抛出异常
 *
 * @exception Error 如果err返回值非0，抛出异常
 *
 * @param err 返回lynError_t的表达试
 */
#define CHECK_ERR(err)                                                                                                 \
    do {                                                                                                               \
        auto _err = (err);                                                                                             \
        if (_err != 0) {                                                                                               \
            throw Error { _err, __FILE__, __LINE__, #err };                                                            \
        }                                                                                                              \
    } while (0)

class Noncopyable {
protected:
    Noncopyable() = default;
    ~Noncopyable() = default;
    Noncopyable(const Noncopyable &) = delete;
    Noncopyable &operator=(const Noncopyable &) = delete;
};

/**
 * @brief 描述可以close的对象
 *
 */
class Closer : Noncopyable {
public:
    /**
     * @brief 关闭对象
     *
     * @exception Error
     */
    virtual void close() = 0;
    virtual ~Closer() = default;
};

using DropErrorHandler = std::function<void(Closer &, const Error &)>;

namespace __inner {
template <typename _ = void>
class DropErrorHandlerSigleton {
public:
    static DropErrorHandler cb;
};

template <typename _>
DropErrorHandler DropErrorHandlerSigleton<_>::cb = [](Closer &, const Error &e) { throw e; };
} // namespace __inner

/**
 * @brief 设置C对象释放失败时的处理函数
 *
 * @param cb
 */
inline void set_drop_error_handler(DropErrorHandler cb) {
    __inner::DropErrorHandlerSigleton<void>::cb = std::move(cb);
}

/**
 * @brief 获取C对象释放失败时的处理函数
 *
 * @return DropErrorHandler&
 */
inline DropErrorHandler &get_drop_error_handler() {
    return __inner::DropErrorHandlerSigleton<void>::cb;
}

/**
 * @brief 将一个C对象实现Closer和析构
 *
 * @tparam T
 */
template <typename T>
struct CObject : public Closer {
    T obj;
    std::function<void(T &obj)> close_fn;
    CObject(T obj, std::function<void(T &obj)> close_fn)
        : obj(std::move(obj))
        , close_fn(std::move(close_fn)) {
    }
    void close() override {
        close_fn(obj);
    }
    ~CObject() {
        try {
            close();
        } catch (const Error &e) {
            get_drop_error_handler()(*this, e);
        }
    }
};
} // namespace lynsdk