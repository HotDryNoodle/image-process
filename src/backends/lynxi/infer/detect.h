/**
 *@file detect.h
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
 *@brief 检测模型推理结果定义
 */

#ifndef __DETECT_H_
#define __DETECT_H_

typedef struct {
  int height;
  int width;
  int ori_height;
  int ori_width;
  float score_threshold;
  float nms_threshold;
  int nms_top_k;
  int is_pad_resize;
  void *output_tensor;
  int class_num;
} YoloPostProcessInfo_t;

// 一个推理结果
typedef struct {
  float xmin;
  float ymin;
  float xmax;
  float ymax;
  float area;
  float score;
  int id;
  const char *class_name;
} BboxResult;

// 推理结果集
typedef struct DetectionResult{
  int boxNum;
  BboxResult *result;
}DetectionResult;


typedef struct Bbox {
	float xmin;
	float ymin;
	float xmax;
	float ymax;

	Bbox() {}

	Bbox(float xmin, float ymin, float xmax, float ymax)
		: xmin(xmin), ymin(ymin), xmax(xmax), ymax(ymax) {}

	~Bbox() {}
} Bbox;

typedef struct Detection {
	int id;
	float score;
	Bbox bbox;
	// const char *class_name;
	Detection() {}

	Detection(int id, float score, Bbox bbox)
		: id(id), score(score), bbox(bbox) {}

	friend bool operator >(const Detection &lhs, const Detection &rhs) {
		return ((lhs.score + (float)lhs.id) > (rhs.score + (float)rhs.id));
	}

	~Detection() {}
  
} Detection;

#endif
