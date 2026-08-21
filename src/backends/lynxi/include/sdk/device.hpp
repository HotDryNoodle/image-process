/**
 * @file device.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief device信息获取相关接口
 * @version 0.1
 * @date 2022-08-22
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "error.hpp"
#include <lyn_api.h>
#include <lyn_smi.h>
#include <string>

namespace lynsdk {
/**
 * @brief 获取device count，device id从0到device count
 *
 * @return int32_t
 */
inline int32_t get_device_cnt() {
    int32_t ret = 0;
    CHECK_ERR(lynGetDeviceCount(&ret));
    return ret;
}

/**
 * @brief 获取device信息
 *
 * @param id device id
 * @return lynDeviceProperties_t
 */
inline lynDeviceProperties_t get_device_prop(int32_t id) {
    lynDeviceProperties_t prop {};
    CHECK_ERR(lynGetDeviceProperties(id, &prop));
    return prop;
}
} // namespace lynsdk
