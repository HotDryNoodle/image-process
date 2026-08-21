/**
 *@file typeConv.h
 *@author lynxi
 *@version v1.0
 *@date 2022-11-11
 *@par Copyright:
 *© 2022 北京灵汐科技有限公司 版权所有。
 * 注意：以下内容均为北京灵汐科技有限公司原创，未经本公司允许，不得转载，否则将视为侵权；对于不遵守此声明或者其他违法使用以下内容者，本公司依法保留追究权。\n
 *© 2022 Lynxi Technologies Co., Ltd. All rights reserved.
 * NOTICE: All information contained here is, and remains the property of Lynxi.
 *This file can not be copied or distributed without the permission of Lynxi
 *Technologies Co., Ltd.
 *@brief 类型转换
 */

#ifndef __TYPE_CONVERSION_H_
#define __TYPE_CONVERSION_H_

#include <inttypes.h>

namespace COMMON {

float half2float(int16_t ib);

int16_t float2half(float value);

}

#endif
