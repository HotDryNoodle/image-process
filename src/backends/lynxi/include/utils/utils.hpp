/**
 * @file utils.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief lynsdk辅助函数库
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

namespace lynsdk {
/**
 * @brief 辅助函数库
 *
 */
namespace utils {
using Seconds = std::chrono::duration<double>;

/**
 * @brief 用于创建一个计时点，两个计时点相减得到一个时间差
 *
 */
class CPUTimePoint {
    std::chrono::high_resolution_clock::time_point point;

public:
    /**
     * @brief Construct a new CPUTimePoint object
     *
     */
    CPUTimePoint() {
        point = std::chrono::high_resolution_clock::now();
    }
    /**
     * @brief self减去start，得到时间差
     *
     * @param start
     * @return std::chrono::duration<int64_t, std::nano>
     */
    Seconds operator-(CPUTimePoint start) const {
        return std::chrono::duration_cast<Seconds>(point - start.point);
    }
};

class CountTimer {
    CPUTimePoint start;
    CPUTimePoint last;
    size_t i = 0;

public:
    template <typename _Rep, typename _Period>
    void
    add(std::chrono::duration<_Rep, _Period> duration, const std::function<void(size_t cnt, Seconds duration)> &func) {
        i++;
        auto cur = CPUTimePoint();
        if (cur - last >= duration) {
            func(i, cur - start);
            last = cur;
        }
    }
};

/**
 * @brief 队列操作超时
 *
 */
class QueueTimeout : public std::exception {
public:
    const char *what() const noexcept override {
        return "queue timeout";
    }
};

/**
 * @brief 操作的队列已经关闭
 *
 */
class QueueClosed : public std::exception {
public:
    const char *what() const noexcept override {
        return "queue closed";
    }
};

/**
 * @brief 一个线程安全队列
 *
 * @tparam T 队列的元素类型
 */
template <typename T>
class Queue {
private:
    std::queue<T> q;
    std::mutex m;
    std::condition_variable c;
    bool closed = false;

public:
    /**
     * @brief 等待一个可用的元素，然后返回该元素
     *
     * @exception QueueTimeout 如果队列以关闭，则抛出QueueClosed异常
     *
     * @return T
     */
    T get() {
        std::unique_lock<std::mutex> l(m);
        c.wait(l, [this]() -> bool {
            if (closed && q.size() == 0) {
                throw QueueClosed();
            }
            return q.size() > 0;
        });
        auto ret = q.front();
        q.pop();
        c.notify_one();
        return ret;
    }
    bool is_closed() {
        std::lock_guard<std::mutex> l(m);
        return closed;
    }
    /**
     * @brief 在给定时间内等待一个可用的元素，然后返回该元素
     *
     * @exception QueueClosed 如果队列以关闭，则抛出QueueClosed异常
     * @exception QueueTimeout 如果超时,则抛出QueueTimeout异常
     *
     * @tparam Rep
     * @tparam Period
     * @param duration
     * @return T
     */
    template <typename Rep = int64_t, typename Period = std::ratio<1>>
    T get_for(std::chrono::duration<Rep, Period> duration = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> l(m);
        if (!c.wait_for(l, duration, [this]() -> bool {
                if (closed && q.size() == 0) {
                    throw QueueClosed();
                }
                return q.size() > 0;
            })) {
            throw QueueTimeout();
        }
        auto ret = q.front();
        q.pop();
        return ret;
    }
    /**
     * @brief 入队一个元素
     *
     * @exception QueueClosed 如果队列以关闭，则抛出QueueClosed异常
     *
     * @param x
     */
    void put(T x) {
        std::lock_guard<std::mutex> l(m);
        if (closed) {
            throw QueueClosed();
        }
        q.push(x);
        c.notify_one();
    }
    /**
     * @brief 返回队列大小
     *
     */
    size_t size() {
        std::lock_guard<std::mutex> l(m);
        return q.size();
    }
    /**
     * @brief 关闭队列，所有正在等待的get和get_for都会抛出QueueClosed异常
     *
     */
    void close() {
        std::lock_guard<std::mutex> l(m);
        closed = true;
        c.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> l(m);
        if (closed) {
            throw QueueClosed();
        }
        c.wait(l, [this]() -> bool {
            if (closed) {
                throw QueueClosed();
            }
            return q.size() == 0;
        });
    }
    
    void open_queue(){
        std::unique_lock<std::mutex> l(m);
        closed = false;
    };

    void notify(){
        std::lock_guard<std::mutex> l(m);
        c.notify_all();
    }

    ~Queue() {
        close();
    }
};

/**
 * @brief Pool用于复用对象
 *
 * @tparam T 对象类型
 */
template <typename T>
class Pool : public Queue<T> {
private:
    std::function<void(T)> destructor;

public:
    /**
     * @brief Construct a new Pool object
     *
     * @param num 初始化对象数量
     * @param constructor 对象构造函数
     * @param destructor 对象析构函数
     */
    Pool(
        size_t num, std::function<T()> constructor = []() -> T { return T(); },
        std::function<void(T)> destructor = [](T) {})
        : destructor(std::move(destructor)) {
        for (size_t i = 0; i < num; i++) {
            this->put(constructor());
        }
    }
    virtual ~Pool() {
        while (this->size() > 0) {
            destructor(this->get());
        }
    }
};

class ThreadPool {
    Queue<std::function<void()>> queue;
    std::vector<std::thread> pool;

public:
    ThreadPool(size_t num_thread, const std::function<void()> &setup) {
        for (size_t i = 0; i < num_thread; i++) {
            pool.emplace_back([setup, &queue = this->queue]() {
                setup();
                while (true) {
                    try {
                        auto f = queue.get();
                        f();
                    } catch (const QueueClosed &e) {
                        return;
                    }
                }
            });
        }
    }
    void run(std::function<void()> f) {
        queue.put(f);
    }
    void wait() {
        queue.wait();
    }
    ~ThreadPool() {
        queue.close();
        for (auto &t : pool) {
            t.join();
        }
    }
};

/**
 * @brief 简化了CPUData和LynData的内存池构造
 *
 * @tparam T
 */
template <typename T>
class DataPool : public Pool<T> {
public:
    DataPool(int n, size_t size_per_batch, size_t batch = 1)
        : Pool<T>(
            n, [size_per_batch, batch]() -> T { return T(size_per_batch, batch); }, [](T ) {}) {};
};

/**
 * @brief 用于排序输出一个队列中的数据，数据必须具有index属性，index属性必须是自然数
 *
 * @tparam I 索引类型
 * @tparam T 对象类型
 */
template <typename I, typename T>
class IndexedQueue {
    std::map<I, T> buffer;
    std::mutex m;
    I cur {};
    I last {};

public:
    /**
     * @brief 入队一个元素，会更具data.index顺序嗲用cb
     *
     * @param index 索引
     * @param data 元素
     * @param cb 回调函数
     */
    void put(I index, T data, const std::function<void(T)> &cb) {
        std::lock_guard<std::mutex> l(m);
        if (index == cur) {
            cb(data);
            cur++;
            for (;; cur++) {
                if (buffer.find(cur) != buffer.end()) {
                    cb(buffer[cur]);
                    buffer.erase(cur);
                } else {
                    break;
                }
            }
            last = cur;
        } else {
            buffer[index] = data;
        }
    }
};

template <typename T>
class Autoput {
    std::shared_ptr<T> state;

public:
    Autoput(T data, Pool<T> &pool)
        : state(std::shared_ptr<T>(new T(data), [&pool](T *t) { pool.put(*t); })) {
    }
    Autoput(T data, std::shared_ptr<Pool<T>> pool)
        : state(std::shared_ptr<T>(new T(data), [pool](T *t) { pool->put(*t); })) {
    }
    T &get() {
        return *state;
    }
    const T &get() const {
        return *state;
    }
};

template <typename T>
Autoput<T> make_autoput(Pool<T> &pool) {
    return Autoput<T>(pool.get(), pool);
}

template <typename T>
Autoput<T> make_autoput(std::shared_ptr<Pool<T>> pool) {
    return Autoput<T>(pool.get(), pool);
}

class Counter {
protected:
    uint64_t add_cnt = 0; // 发送次数,在发送循环中自增
    uint64_t process_cnt = 0; // 处理完毕次数，在 Callback 中自增
    mutable std::mutex mtx;
    std::condition_variable cond;

public:
    // wait number of process == add
    void sync() noexcept {
        std::unique_lock<std::mutex> l(mtx);
        while (process_cnt != add_cnt) {
            cond.wait(l);
        }
    }
    virtual ~Counter() = default;

    // 指令发送循环次数 自增
    bool add(int n = 1) noexcept {
        std::lock_guard<std::mutex> l(mtx);
        add_cnt += n;
        return true;
    }

    // 指令处理完毕次数 自增
    void process(int n = 1) noexcept {
        std::lock_guard<std::mutex> l(mtx);
        process_cnt += n;
        cond.notify_one();
    }
    struct Status {
        uint64_t add_cnt, process_cnt;
    };
    // 获得当前的received，processed
    Status get() const noexcept {
        std::lock_guard<std::mutex> l(mtx);
        return Status { add_cnt, process_cnt };
    }
};

class CountWaiterTimeout : public std::exception {
public:
    const char *what() const noexcept override {
        return "count waiter add timeout";
    }
};

class CountWaiter : public Counter {
private:
    int buf_cnt;

public:
    explicit CountWaiter(int buf_cnt = 10) noexcept
        : buf_cnt(buf_cnt) {
    }
    // if number of add - process large than buf count, then wait for duration, if still large, then throw timeout
    template <typename Rep = int64_t, typename Period = std::ratio<1>>
    bool add(int n = 1, const std::chrono::duration<Rep, Period> &duration = std::chrono::seconds(5)) {
        {
            std::unique_lock<std::mutex> l(mtx);
            while (add_cnt - process_cnt > buf_cnt) {
                if (cond.wait_for(l, duration) == std::cv_status::timeout) {
                    throw CountWaiterTimeout();
                }
            }
        }
        return Counter::add(n);
    }
};

class NotifierTimeout : public std::exception {
public:
    const char *what() const noexcept override {
        return "notifier wait timeout";
    }
};

class Notifier {
    std::mutex m;
    std::condition_variable c;
    bool _is_ready = false;

public:
    /**
     * @brief 等待notify_all被执行
     *
     */
    void wait() {
        std::unique_lock<std::mutex> l(m);
        c.wait(l, [this]() -> bool { return _is_ready; });
    }
    /**
     * @brief 在给定时间内等待notify_all被执行
     *
     * @exception NotifierTimeout 如果超时,则抛出NotifierTimeout异常
     *
     * @tparam Rep
     * @tparam Period
     * @param duration
     */
    template <typename Rep = int64_t, typename Period = std::ratio<1>>
    void wait_for(std::chrono::duration<Rep, Period> duration = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> l(m);
        if (!c.wait_for(l, duration, [this]() -> bool { return _is_ready; })) {
            throw NotifierTimeout();
        }
    }
    /**
     * @brief 终止所有wait和wait_for
     *
     */
    void notify_all() {
        std::lock_guard<std::mutex> l(m);
        _is_ready = true;
        c.notify_all();
    }
    bool is_ready() {
        std::unique_lock<std::mutex> l(m);
        return _is_ready;
    }
};

template <typename T, size_t N>
class CycleIter {
    static_assert(N > 0);
    std::array<T, N> items;
    size_t cur = 0;
    void increase() {
        cur++;
        if (cur == N) {
            cur = 0;
        }
    }

public:
    T &next() {
        increase();
        return get();
    }
    T &get() {
        return items[cur];
    }
    void for_each(std::function<void(T &)> cb) {
        for (auto &item : items) {
            cb(item);
        }
    }
};

class SequenceNotifier {
    std::mutex m;
    std::condition_variable cond;
    size_t cur = 0;

public:
    void wait(size_t i) {
        std::unique_lock<std::mutex> l(m);
        cond.wait(l, [this, i]() -> bool { return i == cur; });
        cur++;
    }
};

// FIXME: inaccuracy, +1%
/**
 * @brief 每interval运行fn，直到fn返回false，fn接受调用次数，从0开始
 *
 * @tparam Rep
 * @tparam Period
 * @param interval
 * @param fn
 */
template <typename Rep, typename Period>
void run_interval(const std::chrono::duration<Rep, Period> &interval, const std::function<bool(size_t times)> &fn) {
    auto ok = true;
    auto nextPoint = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; ok; i++) {
        ok = fn(i);
        nextPoint += interval;
        std::this_thread::sleep_until(nextPoint);
    }
}
} // namespace utils
} // namespace lynsdk
