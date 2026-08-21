#include "yolovXPostProcess.h"

#include <sys/time.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include <cstdio>
#include <cstdint>

#include "typeConv.h"

#define CLASS_NUM (2)  // 2//80

using namespace COMMON;

enum class YoloModleType { x7 = 0, normal };

constexpr YoloModleType cur_type = YoloModleType::normal;

class Tensor3D {
public:
    Tensor3D(int16_t* data, int b, int c, int h)
        : data_(data), b_(b), c_(c), h_(h) {}

    // 返回引用，支持读写
    int16_t& at(int bi, int ci, int hi) {
        assert(bi >= 0 && bi < b_);
        assert(ci >= 0 && ci < c_);
        assert(hi >= 0 && hi < h_);
        return data_[((bi * c_ + ci) * h_ + hi)];
    }

    // 只读版本
    int16_t at(int bi, int ci, int hi) const {
        return const_cast<Tensor3D*>(this)->at(bi, ci, hi);
    }

private:
    int16_t* data_;
    int b_, c_, h_;
};

Yolov5sConfig default_yolov5s_config = {
    {8, 16, 32},
    {{{10, 13}, {16, 30}, {33, 23}},
     {{30, 61}, {62, 45}, {59, 119}},
     {{116, 90}, {156, 198}, {373, 326}}},
    CLASS_NUM,
    {"Bigship",       "Carrier",      "car",
     "motorcycle",    "airplane",     "bus",
     "train",         "truck",        "boat",
     "traffic light", "fire hydrant", "stop sign",
     "parking meter", "bench",        "bird",
     "cat",           "dog",          "horse",
     "sheep",         "cow",          "elephant",
     "bear",          "zebra",        "giraffe",
     "backpack",      "umbrella",     "handbag",
     "tie",           "suitcase",     "frisbee",
     "skis",          "snowboard",    "sports ball",
     "kite",          "baseball bat", "baseball glove",
     "skateboard",    "surfboard",    "tennis racket",
     "bottle",        "wine glass",   "cup",
     "fork",          "knife",        "spoon",
     "bowl",          "banana",       "apple",
     "sandwich",      "orange",       "broccoli",
     "carrot",        "hot dog",      "pizza",
     "donut",         "cake",         "chair",
     "couch",         "potted plant", "bed",
     "dining table",  "toilet",       "tv",
     "laptop",        "mouse",        "remote",
     "keyboard",      "cell phone",   "microwave",
     "oven",          "toaster",      "sink",
     "refrigerator",  "book",         "clock",
     "vase",          "scissors",     "teddy bear",
     "hair drier",    "toothbrush"}};

template <class ForwardIterator>
inline size_t argmin(ForwardIterator first, ForwardIterator last) {
    return std::distance(first, std::min_element(first, last));
}

template <class ForwardIterator>
inline size_t argmax(ForwardIterator first, ForwardIterator last) {
    return std::distance(first, std::max_element(first, last));
}

void yolo5Nms(std::vector<Detection> &input,
              float                   iou_threshold,
              int                     top_k,
              std::vector<Detection> &result,
              bool                    suppress) {
    std::stable_sort(input.begin(), input.end(), std::greater<Detection>());
    // auto input = LMXprocessDetections(input_ori);

    std::vector<bool> skip(input.size(), false);

    std::vector<float> areas;
    areas.reserve(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        float width  = input[i].bbox.xmax - input[i].bbox.xmin;
        float height = input[i].bbox.ymax - input[i].bbox.ymin;
        areas.push_back(width * height);
    }

    int count = 0;
    for (size_t i = 0; /*count < top_k && */ i < skip.size(); i++) {
        if (skip[i]) { continue; }
        skip[i] = true;
        ++count;

        for (size_t j = i + 1; j < skip.size(); ++j) {
            if (skip[j]) { continue; }
            if (suppress == false) {
                if (input[i].id != input[j].id) { continue; }
            }

            float xx1 = std::max(input[i].bbox.xmin, input[j].bbox.xmin);
            float yy1 = std::max(input[i].bbox.ymin, input[j].bbox.ymin);
            float xx2 = std::min(input[i].bbox.xmax, input[j].bbox.xmax);
            float yy2 = std::min(input[i].bbox.ymax, input[j].bbox.ymax);

            if (xx2 > xx1 && yy2 > yy1) {
                float area_intersection = (xx2 - xx1) * (yy2 - yy1);
                float iou_ratio         = area_intersection /
                                  (areas[j] + areas[i] - area_intersection);
                if (iou_ratio > iou_threshold) { skip[j] = true; }
            }
        }
        result.push_back(input[i]);
    }
}

void tensorpostV5Process(void                   *tensor,
                       YoloPostProcessInfo_t  *post_info,
                       int                     layer,
                       std::vector<Detection> &dets) {
    // auto *data = reinterpret_cast<uint16_t *>(tensor);
    void *data      = tensor;
    int   class_num = default_yolov5s_config.class_num;
    int   stride    = default_yolov5s_config.strides[layer];

    const int num_pred = []() {
        if (cur_type == YoloModleType::x7) {
            return (4 + 1 + 1 +
                    1);  // xyxy + c + idx(最高置信度值的类别) + cls(置信度)
        }
        else { return (default_yolov5s_config.class_num + 4 + 1); }
    }();

    std::vector<int16_t> class_pred(default_yolov5s_config.class_num, 0);

    std::vector<std::pair<double, double> > &anchors =
        default_yolov5s_config.anchors_table[layer];

    double h_ratio      = post_info->height * 1.0 / post_info->ori_height;
    double w_ratio      = post_info->width * 1.0 / post_info->ori_width;
    double resize_ratio = std::min(w_ratio, h_ratio);
    if (post_info->is_pad_resize) {
        w_ratio = resize_ratio;
        h_ratio = resize_ratio;
    }

    int grid_height, grid_width;
    grid_height = post_info->height / stride;
    grid_width  = post_info->width / stride;

    int16_t box_score_threshold = float2half(post_info->score_threshold);
    for (int h = 0; h < grid_height; h++) {
        for (int w = 0; w < grid_width; w++) {
            for (size_t k = 0; k < anchors.size(); k++) {
                int16_t *cur_data = (int16_t *)data + k * num_pred;
                int16_t  objness  = cur_data[4];

                if (objness <
                    box_score_threshold /*post_info->score_threshold*/) {
                    continue;
                }

                int16_t id         = -1;
                double  confidence = 0;
                if (cur_type == YoloModleType::x7) {
                    id         = cur_data[5];
                    confidence = half2float(objness) * half2float(cur_data[6]);
                }
                else {
                    for (int index = 0; index < class_num; ++index) {
                        class_pred[index] = (cur_data[5 + index]);
                    }
                    id = argmax(class_pred.begin(), class_pred.end());
                    confidence =
                        half2float(objness) * half2float(class_pred[id]);
                }

                if (confidence < post_info->score_threshold) { continue; }

                float center_x = half2float(cur_data[0]);
                float center_y = half2float(cur_data[1]);
                float scale_x  = half2float(cur_data[2]);
                float scale_y  = half2float(cur_data[3]);

                double xmin = (center_x - scale_x / 2.0);
                double ymin = (center_y - scale_y / 2.0);
                double xmax = (center_x + scale_x / 2.0);
                double ymax = (center_y + scale_y / 2.0);
                double w_padding =
                    (post_info->width - w_ratio * post_info->ori_width) / 2.0;
                double h_padding =
                    (post_info->height - h_ratio * post_info->ori_height) / 2.0;

                double xmin_org = (xmin - w_padding) / w_ratio;
                double xmax_org = (xmax - w_padding) / w_ratio;
                double ymin_org = (ymin - h_padding) / h_ratio;
                double ymax_org = (ymax - h_padding) / h_ratio;

                if (xmax_org <= 0 || ymax_org <= 0) { continue; }

                if (xmin_org > xmax_org || ymin_org > ymax_org) { continue; }

                xmin_org = std::max(xmin_org, 0.0);
                xmax_org = std::min(xmax_org, post_info->ori_width - 1.0);
                ymin_org = std::max(ymin_org, 0.0);
                ymax_org = std::min(ymax_org, post_info->ori_height - 1.0);

                Bbox bbox(xmin_org, ymin_org, xmax_org, ymax_org);
                dets.push_back(Detection((int)id, confidence, bbox));
            }
            data = (int16_t *)data + num_pred * anchors.size();
        }
    }
}

std::vector<Detection> Yolov5sPostProcess(YoloPostProcessInfo_t *post_info) {
    std::vector<Detection> det_restuls;
    std::vector<Detection> dets;

    // Calculate resize ratios (same as original implementation)
    double h_ratio = post_info->height * 1.0 / post_info->ori_height;
    double w_ratio = post_info->width * 1.0 / post_info->ori_width;
    double resize_ratio = std::min(w_ratio, h_ratio);
    if (post_info->is_pad_resize) {
        w_ratio = resize_ratio;
        h_ratio = resize_ratio;
    }
    // Calculate total number of detections from all layers
    int grid_height1 = post_info->height / default_yolov5s_config.strides[0];
    int grid_width1 = post_info->width / default_yolov5s_config.strides[0];
    int detections_layer1 = grid_height1 * grid_width1 * default_yolov5s_config.anchors_table[0].size();

    int grid_height2 = post_info->height / default_yolov5s_config.strides[1];
    int grid_width2 = post_info->width / default_yolov5s_config.strides[1];
    int detections_layer2 = grid_height2 * grid_width2 * default_yolov5s_config.anchors_table[1].size();

    int grid_height3 = post_info->height / default_yolov5s_config.strides[2];
    int grid_width3 = post_info->width / default_yolov5s_config.strides[2];
    int detections_layer3 = grid_height3 * grid_width3 * default_yolov5s_config.anchors_table[2].size();

    int total_detections = detections_layer1 + detections_layer2 + detections_layer3;

    // Determine prediction format based on model type
    const int num_pred = []() {
        if (cur_type == YoloModleType::x7) {
            return (4 + 1 + 1 + 1);  // xyxy + objness + class_id + confidence
        }
        else {
            return (default_yolov5s_config.class_num + 4 + 1);  // class_scores + xyxy + objness
        }
    }();

    // YOLOv5 output format: [batch_size, total_detections, num_pred]
    // We use Tensor3D to access the data more intuitively
    Tensor3D tensor((int16_t*)post_info->output_tensor, 1, total_detections, num_pred);

    // Process all detections
    for (int i = 0; i < total_detections; ++i) {
        // Get objectness score
        int16_t objness = tensor.at(0, i, 4);

        // Filter by objectness threshold (convert threshold to half precision for comparison)
        int16_t objness_threshold = float2half(post_info->score_threshold);
        if (objness < objness_threshold) {
            continue;
        }

        int class_id = -1;
        float confidence = 0.0f;

        if (cur_type == YoloModleType::x7) {
            // For x7 format: [x, y, w, h, objness, class_id, confidence]
            class_id = (int)tensor.at(0, i, 5);
            float obj_conf = half2float(objness);
            float cls_conf = half2float(tensor.at(0, i, 6));
            confidence = obj_conf * cls_conf;
        }
        else {
            // For normal format: [x, y, w, h, objness, class_score1, class_score2, ...]
            // Find class with highest score
            float max_score = 0.0f;
            for (int j = 0; j < default_yolov5s_config.class_num; ++j) {
                float score = half2float(tensor.at(0, i, 5 + j));
                if (score > max_score) {
                    max_score = score;
                    class_id = j;
                }
            }
            confidence = half2float(objness) * max_score;
        }

        // Filter by confidence threshold
        if (confidence < post_info->score_threshold) {
            continue;
        }

        // Get bounding box coordinates (center_x, center_y, width, height)
        float center_x = half2float(tensor.at(0, i, 0));
        float center_y = half2float(tensor.at(0, i, 1));
        float scale_x = half2float(tensor.at(0, i, 2));
        float scale_y = half2float(tensor.at(0, i, 3));

        // Convert from center+scale format to min+max format
        double xmin = (center_x - scale_x / 2.0);
        double ymin = (center_y - scale_y / 2.0);
        double xmax = (center_x + scale_x / 2.0);
        double ymax = (center_y + scale_y / 2.0);

        // Calculate padding (using the potentially different w_ratio and h_ratio)
        double w_padding = (post_info->width - w_ratio * post_info->ori_width) / 2.0;
        double h_padding = (post_info->height - h_ratio * post_info->ori_height) / 2.0;

        // Transform to original image coordinates
        double xmin_org = (xmin - w_padding) / w_ratio;
        double xmax_org = (xmax - w_padding) / w_ratio;
        double ymin_org = (ymin - h_padding) / h_ratio;
        double ymax_org = (ymax - h_padding) / h_ratio;

        // Validation checks
        if (xmax_org <= 0 || ymax_org <= 0) {
            continue;
        }

        if (xmin_org > xmax_org || ymin_org > ymax_org) {
            continue;
        }

        // Clamp to image bounds
        xmin_org = std::max(xmin_org, 0.0);
        xmax_org = std::min(xmax_org, post_info->ori_width - 1.0);
        ymin_org = std::max(ymin_org, 0.0);
        ymax_org = std::min(ymax_org, post_info->ori_height - 1.0);

        Bbox bbox(xmin_org, ymin_org, xmax_org, ymax_org);
        dets.push_back(Detection(class_id, confidence, bbox));
    }

    yolo5Nms(dets, post_info->nms_threshold, post_info->nms_top_k, det_restuls, false);
    return det_restuls;
}

std::vector<Detection> Yolov8sPostProcess(YoloPostProcessInfo_t *post_info){
    std::vector<Detection> det_restuls;
    std::vector<Detection> dets;
    const int knum_detections = 8400;
    // Calculate resize ratios (same as tensorpostV9Process)
    double h_ratio = post_info->height * 1.0 / post_info->ori_height;
    double w_ratio = post_info->width * 1.0 / post_info->ori_width;
    double resize_ratio = std::min(w_ratio, h_ratio);
    if (post_info->is_pad_resize) {
        w_ratio = resize_ratio;
        h_ratio = resize_ratio;
    }
    // YOLOv8 output format: [batch_size, 8400, 2 + 4]
    Tensor3D tensor((int16_t*)post_info->output_tensor, 
                        1, knum_detections, post_info->class_num + 4);
    for (int i = 0; i < knum_detections; ++i) {
        // Find highest confidence class
        float max_conf = 0.0f;
        int class_id = -1;
        for (int j = 0; j < post_info->class_num; ++j) {
            float conf = half2float(tensor.at(0, i, 4 + j));
            if (conf > max_conf) {
                max_conf = conf;
                class_id = j;
            }
        }
        // Filter low confidence detections
        if (max_conf < post_info->score_threshold) {
            continue;
        }

        // Get bounding box coordinates
        float center_x = half2float(tensor.at(0, i, 0));
        float center_y =  half2float(tensor.at(0, i, 1));
        float scale_x =  half2float(tensor.at(0, i, 2));
        float scale_y =  half2float(tensor.at(0, i, 3));
         // Convert bbox format: center+scale -> min+max (same as tensorpostV9Process)
        double xmin = (center_x - scale_x / 2.0);
        double ymin = (center_y - scale_y / 2.0);
        double xmax = (center_x + scale_x / 2.0);
        double ymax = (center_y + scale_y / 2.0);
        
        // Calculate padding (using the potentially different w_ratio and h_ratio)
        double w_padding = (post_info->width - w_ratio * post_info->ori_width) / 2.0;
        double h_padding = (post_info->height - h_ratio * post_info->ori_height) / 2.0;
        
        // Transform to original image coordinates
        double xmin_org = (xmin - w_padding) / w_ratio;
        double xmax_org = (xmax - w_padding) / w_ratio;
        double ymin_org = (ymin - h_padding) / h_ratio;
        double ymax_org = (ymax - h_padding) / h_ratio;
        
        // Validation checks (same as tensorpostV9Process)
        if (xmax_org <= 0 || ymax_org <= 0) {
            continue;
        }
        
        if (xmin_org > xmax_org || ymin_org > ymax_org) {
            continue;
        }
        
        // Clamp to image bounds
        xmin_org = std::max(xmin_org, 0.0);
        xmax_org = std::min(xmax_org, post_info->ori_width - 1.0);
        ymin_org = std::max(ymin_org, 0.0);
        ymax_org = std::min(ymax_org, post_info->ori_height - 1.0);
        
        Bbox bbox(xmin_org, ymin_org, xmax_org, ymax_org);
        dets.push_back(Detection(class_id, max_conf, bbox));

    }
    yolo5Nms(dets, post_info->nms_threshold, post_info->nms_top_k, det_restuls, false);
    return det_restuls;
}

std::vector<Detection> Yolov9sPostProcess(YoloPostProcessInfo_t *post_info){
	std::vector<Detection> det_restuls;
    std::vector<Detection> dets;
    const int knum_detections = 8400;
    
    // Calculate resize ratios (same as tensorpostV9Process)
    double h_ratio = post_info->height * 1.0 / post_info->ori_height;
    double w_ratio = post_info->width * 1.0 / post_info->ori_width;
    double resize_ratio = std::min(w_ratio, h_ratio);
    if (post_info->is_pad_resize) {
        w_ratio = resize_ratio;
        h_ratio = resize_ratio;
    }
    
    // YOLOv9 output format: [batch_size, 4 + class_num, 8400]
    Tensor3D tensor((int16_t*)post_info->output_tensor, 
                        1, post_info->class_num + 4, knum_detections);

    for (int i = 0; i < knum_detections; ++i) {
        // Find highest confidence class
        float max_conf = 0.0f;
        int class_id = -1;
        for (int j = 0; j < post_info->class_num; ++j) {
            float conf = half2float(tensor.at(0, 4 + j, i));
            if (conf > max_conf) {
                max_conf = conf;
                class_id = j;
            }
        }
        
        // Filter low confidence detections
        if (max_conf < post_info->score_threshold) {
            continue;
        }
        
        // Get bounding box coordinates
        float center_x = half2float(tensor.at(0, 0, i));
        float center_y = half2float(tensor.at(0, 1, i));
        float scale_x = half2float(tensor.at(0, 2, i));
        float scale_y = half2float(tensor.at(0, 3, i));
        
        // Convert bbox format: center+scale -> min+max (same as tensorpostV9Process)
        double xmin = (center_x - scale_x / 2.0);
        double ymin = (center_y - scale_y / 2.0);
        double xmax = (center_x + scale_x / 2.0);
        double ymax = (center_y + scale_y / 2.0);
        
        // Calculate padding (using the potentially different w_ratio and h_ratio)
        double w_padding = (post_info->width - w_ratio * post_info->ori_width) / 2.0;
        double h_padding = (post_info->height - h_ratio * post_info->ori_height) / 2.0;
        
        // Transform to original image coordinates
        double xmin_org = (xmin - w_padding) / w_ratio;
        double xmax_org = (xmax - w_padding) / w_ratio;
        double ymin_org = (ymin - h_padding) / h_ratio;
        double ymax_org = (ymax - h_padding) / h_ratio;
        
        // Validation checks (same as tensorpostV9Process)
        if (xmax_org <= 0 || ymax_org <= 0) {
            continue;
        }
        
        if (xmin_org > xmax_org || ymin_org > ymax_org) {
            continue;
        }
        
        // Clamp to image bounds
        xmin_org = std::max(xmin_org, 0.0);
        xmax_org = std::min(xmax_org, post_info->ori_width - 1.0);
        ymin_org = std::max(ymin_org, 0.0);
        ymax_org = std::min(ymax_org, post_info->ori_height - 1.0);
        
        Bbox bbox(xmin_org, ymin_org, xmax_org, ymax_org);
        dets.push_back(Detection(class_id, max_conf, bbox));
    }
    
    yolo5Nms(dets, post_info->nms_threshold, post_info->nms_top_k, det_restuls, false);
    return det_restuls;
}