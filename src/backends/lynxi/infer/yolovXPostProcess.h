/**
 *@file yolov5sPostProcess.h
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
 *@brief yolov5s后处理
 */

#ifndef __YOLO5S_POST_PROCESS_H_
#define __YOLO5S_POST_PROCESS_H_

#include <string>
#include <vector>

#include "detect.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

struct Yolov5sConfig {
    std::vector<int>                                    strides;
    std::vector<std::vector<std::pair<double, double>>> anchors_table;
    int                                                 class_num;
    std::vector<std::string>                            class_names;
};

/**
 * @brief apu推理结果转换成DetectionResult
 *
 * @param[in] post_info yolov5s后处理信息
 * @return 无
 */
std::vector<Detection> Yolov5sPostProcess(YoloPostProcessInfo_t *post_info);

/**
 * @brief apu推理结果转换成DetectionResult
 *
 * @param[in] post_info yolov8s后处理信息
 * @return 无
 */
std::vector<Detection> Yolov8sPostProcess(YoloPostProcessInfo_t *post_info);

/**
 * @brief apu推理结果转换成DetectionResult
 *
 * @param[in] post_info yolov5s后处理信息
 * @return 无
 */
std::vector<Detection> Yolov9sPostProcess(YoloPostProcessInfo_t *post_info);

/**
 * @brief yolo5Nms
 *
 * @param[in] post_info yolov5s后处理信息
 * @return 无
 */
void yolo5Nms(std::vector<Detection> &input,
              float                   iou_threshold,
              int                     top_k,
              std::vector<Detection> &result,
              bool                    suppress);

// #ifdef __cplusplus
// }
// #endif

#endif
