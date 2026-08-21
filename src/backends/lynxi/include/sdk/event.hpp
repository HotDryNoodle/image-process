/**
 * @file event.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief event封装和增强
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "../utils/utils.hpp"
#include "error.hpp"
#include <memory>

namespace lynsdk {
/**
 * @brief 封装event接口
 *
 */
class Event {
    static void close(lynEvent_t &p) {
        if (p) {
            CHECK_ERR(lynDestroyEvent(p));
            p = nullptr;
        }
    }
    std::shared_ptr<CObject<lynEvent_t>> p { nullptr };

public:
    Event() {
        lynEvent_t e = nullptr;
        CHECK_ERR(lynCreateEvent(&e));
        p = std::make_shared<CObject<lynEvent_t>>(e, close);
    }
    lynStream_t get() const {
        return p->obj;
    }
    /**
     * @brief 计算两个event的时间差
     *
     * @param start
     * @return utils::Seconds
     */
    utils::Seconds operator-(const Event &start) const {
        float ms = 0;
        CHECK_ERR(lynEventElapsedTime(start.get(), p->obj, &ms));
        return utils::Seconds(double(ms) / 1000);
    }
};
} // namespace lynsdk