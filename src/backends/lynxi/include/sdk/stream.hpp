/**
 * @file stream.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief 封装stream接口
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "error.hpp"
#include "event.hpp"
#ifdef ENABLE_CPP_COROUTINE
#include <coroutine>
#endif
#include <functional>
#include <memory>

namespace lynsdk {
/**
 * @brief 封装stream接口
 *
 */
class Stream {
    struct EventTrigger {
        CObject<lynStream_t> p { nullptr, close };
        Event event;
        void trigger() {
            CHECK_ERR(lynRecordEvent(p.obj, event.get()));
        }
    };
    std::shared_ptr<EventTrigger> event_trigger { nullptr };

    static void close(lynStream_t &p) {
        if (p) {
            CHECK_ERR(lynDestroyStream(p));
            p = nullptr;
        }
    }
    CObject<lynStream_t> p { nullptr, close };
#ifdef ENABLE_CPP_COROUTINE
    struct Awaitable {
        Stream &s;
        bool await_ready() {
            return false;
        }
        void await_suspend(std::coroutine_handle<> h) {
            s.add_callback([h] { h.resume(); });
        }
        void await_resume() {
        }
    };
#endif

public:
    Stream() {
        CHECK_ERR(lynCreateStream(&p.obj));
    }
    /**
     * @brief 添加callback到stream
     *
     * @warning
     * stream执行到callback时，会通知主机执行callback，但不会等待callback执行完而是继续执行下一条指令，stream析构时会等待执行完成
     *
     * @param cb
     */
    void add_callback(std::function<void()> cb) const {
        CHECK_ERR(lynStreamAddCallback(
            p.obj,
            [](void *_ud) -> lynError_t {
                try {
                    auto ud = static_cast<std::function<void()> *>(_ud);
                    (*ud)();
                    delete ud;
                } catch (const Error &e) {
                    return e.code();
                }
                return 0;
            },
            new std::function<void()>(std::move(cb))));
    }
    /**
     * @brief 添加callback到stream
     *
     * @param cb
     * @param userData
     */
    void add_callback(lynStreamCallback_t cb, void *userData) const {
        CHECK_ERR(lynStreamAddCallback(p.obj, cb, userData));
    }
    /**
     * @brief 增加一个callback到stream，但阻塞stream，直到callback执行完成
     *
     * @param cb
     */
    void add_command(const std::function<void()> &cb) {
        if (event_trigger == nullptr) {
            event_trigger = std::make_shared<EventTrigger>();
        }
        add_callback([cb, event_trigger = event_trigger] {
            cb();
            event_trigger->trigger();
        });
        wait_event(event_trigger->event);
    }
    /**
     * @brief 等待stream中的指令执行完
     *
     * @warning callback只会触发调用，不会等待执行完成，析构时才会等待执行完成
     *
     */
    void wait() const {
        CHECK_ERR(lynSynchronizeStream(p.obj));
    }
    /**
     * @brief 等待stream中的指令执行完并销毁stream，会等待callback执行完成
     *
     */
    void wait_and_close() {
        wait();
        p.close();
    }
    lynStream_t get() const {
        return p.obj;
    }
    void add_event(const Event &e) const {
        CHECK_ERR(lynRecordEvent(get(), e.get()));
    }
    void wait_event(const Event &e) const {
        CHECK_ERR(lynStreamWaitEvent(get(), e.get()));
    }
#ifdef ENABLE_CPP_COROUTINE
    Awaitable await() {
        return Awaitable { *this };
    }
#endif
};

#ifdef ENABLE_CPP_COROUTINE
struct Task {
    struct promise_type {
        Task get_return_object() {
            return {};
        }
        std::suspend_never initial_suspend() {
            return {};
        }
        std::suspend_never final_suspend() noexcept {
            return {};
        }
        void return_void() {
        }
        void unhandled_exception() {
            throw;
        }
    };
    std::coroutine_handle<promise_type> coro;
};
#endif

/**
 * @brief 基于Event封装的时间点
 *
 */
class LynTimePoint : public Event {
public:
    LynTimePoint() = default;
    LynTimePoint(const Stream &s) {
        s.add_event(*this);
    }
    void record(const Stream &s) {
        s.add_event(*this);
    }
};

inline utils::Autoput<LynTimePoint> record_lyntimepoint(const Stream &s, utils::Pool<LynTimePoint> &pool) {
    auto start = utils::make_autoput(pool);
    start.get().record(s);
    return start;
}
} // namespace lynsdk