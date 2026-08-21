/**
 *@file func.h
 *@author lynxi
 *@version v1.0
 *@date 2022-09-13
 *@par Copyright:
 *© 2022 北京灵汐科技有限公司 版权所有。
 * 注意：以下内容均为北京灵汐科技有限公司原创，未经本公司允许，不得转载，否则将视为侵权；对于不遵守此声明或者其他违法使用以下内容者，本公司依法保留追究权。\n
 *© 2022 Lynxi Technologies Co., Ltd. All rights reserved.
 * NOTICE: All information contained here is, and remains the property of Lynxi.
 *This file can not be copied or distributed without the permission of Lynxi
 *Technologies Co., Ltd.
 *@brief 提供常用函数
 */

#ifndef __LYNXI_FUNC_H_
#define __LYNXI_FUNC_H_

#include <functional>
#include <future>
#include <random>
#include <vector>
#include "lynsdk.hpp"

using namespace std;
using namespace lynsdk;
using namespace utils;

// FIXME: inaccuracy, +1%
// call fn every interval, end while fn return false
template <typename Rep, typename Period>
future<void> setInterval(const chrono::duration<Rep, Period> &interval, const function<bool(size_t times)> &fn) {
  return async(launch::async, [fn, interval]() {
    auto ok = true;
    auto nextPoint = chrono::high_resolution_clock::now();
    for (size_t i = 0; ok; i++) {
      ok = fn(i);
      nextPoint += interval;
      this_thread::sleep_until(nextPoint);
    }
  });
}

// filled with rand integers
template <typename DType = uint8_t>
enable_if_t<is_integral<DType>::value, void> fillRandData(CPUData& cpuData, DType min, DType max) {
  default_random_engine e;
  uniform_int_distribution<DType> u(min, max);
  for (size_t i = 0; i < cpuData.get_size(); i++) {
    *(cpuData.pointer(0, i)) = u(e);
  }
}

// filled with rand floating_points
template <typename DType>
enable_if_t<is_floating_point<DType *>::value, void> fillRandData(CPUData& cpuData, DType min, DType max) {
  default_random_engine e;
  uniform_real_distribution<DType> u(min, max);
  for (size_t i = 0; i < cpuData.get_size(); i++) {
    *(cpuData.pointer(0, i)) = u(e);
  }
}

// 获取时间间隔(ms)
template <typename T>
double getTimeInterval(const T& begin, const T& end) {
  return (double)chrono::duration_cast<chrono::microseconds>(end - begin).count() / 1e3;
}

// 解析芯片id
inline vector<uint32_t> getChipIDs(const string& chipIds) {
  vector<uint32_t> vChipId;
  size_t start = 0;
  size_t end = 0;
  while (end != string::npos) {
    end = chipIds.find(",", start);
    auto s = chipIds.substr(start, end);
    vChipId.push_back(atoi(s.c_str()));
    start = end + 1;
  }
  return vChipId;
}

#endif
