/**
 * @file lynsdk.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief sdk的c++封装、各模块增强、辅助函数库的头文件集合
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "sdk/context.hpp"
#include "sdk/device.hpp"
#include "sdk/error.hpp"
#include "sdk/event.hpp"
#include "sdk/image.hpp"
#include "sdk/ipe.hpp"
#include "sdk/memory.hpp"
#include "sdk/model.hpp"
#include "sdk/plugin.hpp"
#include "sdk/stream.hpp"
#include "sdk/video.hpp"
#include "utils/cpu_data.hpp"
#include "utils/utils.hpp"

/**
 * @brief lynsdk库
 *
 */
namespace lynsdk {} // namespace lynsdk

/**
 * @example example_model.cpp
 * @example example_ipe.cpp
 * @example example_image_decode_soft.cpp
 * @example example_video_demux.cpp
 * @example example_video_decode.cpp
 * @example example_video_encode.cpp
 * @example example_video_transcode.cpp
 * @example example_image_predict_by_mobilenet.cpp
 * @example example_image_async_decode_encode.cpp
 * @example example_image_decode_encode.cpp
 * @example example_plugin.cpp
 * @example example_plugin_callback.cpp
 * @example example_plugin_coroutine.cpp
 * @example example_coroutine.cpp
 * @example example_lyntimepoint.cpp
 *
 */