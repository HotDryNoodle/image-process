/**
 *@file smi.h
 *@author lynxi
 *@version v1.0
 *@date 2022-08-26
 *@par Copyright:
 *© 2022 北京灵汐科技有限公司 版权所有。
 * 注意：以下内容均为北京灵汐科技有限公司原创，未经本公司允许，不得转载，否则将视为侵权；对于不遵守此声明或者其他违法使用以下内容者，本公司依法保留追究权。\n
 *© 2022 Lynxi Technologies Co., Ltd. All rights reserved.
 * NOTICE: All information contained here is, and remains the property of Lynxi.
 *This file can not be copied or distributed without the permission of Lynxi
 *Technologies Co., Ltd.
 *@brief 获取芯片状态信息
 */

#ifndef __SMI_H_
#define __SMI_H_

#include <stdint.h>

// 芯片信息
typedef struct {
  uint32_t apuUsage;
  uint32_t cpuUsage;
  uint32_t vicUsage;
  float memoryUsage;
  uint32_t ipeFPS;
  int32_t temperature;
  float power;
} SMI_INFO_T;

/********************************************************************************
类名 : SMI(芯片信息类)
Description: 提供了获取芯片信息的接口
*******************************************************************************/
class SMI {
  uint8_t id;

public:
  SMI(uint8_t id) : id(id) {}

  SMI_INFO_T get();
};

#endif