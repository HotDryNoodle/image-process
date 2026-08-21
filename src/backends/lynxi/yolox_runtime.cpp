/* First-party adapter of MSF YolovXProcessor inference (single chip).
 * Tile/pre/infer/post threads follow
 * msf/source/filter/yolox_lynxi_processor.cpp.
 */
#include "yolox_runtime.hpp"

#include <gst/gst.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <utility>

#include "environment.h"
#include "func.h"
#include "lynsdk.hpp"
#include "yolovXPostProcess.h"

namespace lyn_param {

enum class EChannelType { gray = 0, rgb, bgr, undefined };
enum class EYoloType { yolov5 = 0, yolov8, yolov9, undefined };

struct YoloParam {
    float        score_threshold = 0.25F;
    float        nms_threshold   = 0.45F;
    int          nms_top_k       = 500;
    EChannelType type            = EChannelType::rgb;
    EYoloType    yolo_type       = EYoloType::yolov8;
    int          class_num       = 2;
};

YoloParam kYoloParam;

}  // namespace lyn_param

namespace {

using LynData = lynsdk::LynData;
using CPUData = lynsdk::CPUData;
using Stream  = lynsdk::Stream;
using Model   = lynsdk::Model;
using lynsdk::utils::CPUTimePoint;
using lynsdk::utils::DataPool;
using lynsdk::utils::Queue;
using lynsdk::utils::QueueClosed;

constexpr int kModelCount = 1;
constexpr int kStreamNum  = 1;

struct ImageInfo {
    std::uint32_t x_offset = 0;
    std::uint32_t y_offset = 0;
    std::size_t   label    = 0;
};

struct Image4Infer {
    LynData   imgData;
    ImageInfo imgInfo;
};

struct InferRes {
    CPUData   inferData;
    ImageInfo imgInfo;
};

class Cutter {
  public:
    Cutter(std::size_t width,
           std::size_t height,
           std::size_t crop,
           std::size_t stride)
        : width_(width), height_(height), crop_(crop), stride_(stride) {}

    bool get_next(ImageInfo& res) {
        const std::size_t max_x = static_cast<std::size_t>(std::ceil(
                                      static_cast<float>(width_ - crop_) /
                                      static_cast<float>(stride_))) +
                                  1U;
        const std::size_t max_y = static_cast<std::size_t>(std::ceil(
                                      static_cast<float>(height_ - crop_) /
                                      static_cast<float>(stride_))) +
                                  1U;
        std::lock_guard<std::mutex> lock(mutex_);
        ++index_;
        if (index_ > (max_x * max_y)) { return false; }
        res.x_offset = ((index_ - 1) % max_x) * stride_;
        res.y_offset = ((index_ - 1) / max_x) * stride_;
        res.x_offset =
            (res.x_offset + crop_ > width_) ? (width_ - crop_) : res.x_offset;
        res.y_offset =
            (res.y_offset + crop_ > height_) ? (height_ - crop_) : res.y_offset;
        return true;
    }

  private:
    std::size_t width_;
    std::size_t height_;
    std::size_t crop_;
    std::size_t stride_;
    std::size_t index_ = 0;
    std::mutex  mutex_;
};

cv::Mat make_model_input(const cv::Mat& input) {
    cv::Mat output;
    if (input.channels() == 1) {
        switch (lyn_param::kYoloParam.type) {
            case lyn_param::EChannelType::gray:
                output = input.clone();
                break;
            case lyn_param::EChannelType::bgr:
                cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
                break;
            case lyn_param::EChannelType::rgb:
            default:
                cv::cvtColor(input, output, cv::COLOR_GRAY2RGB);
                break;
        }
    }
    else {
        switch (lyn_param::kYoloParam.type) {
            case lyn_param::EChannelType::gray:
                cv::cvtColor(input, output, cv::COLOR_RGB2GRAY);
                break;
            case lyn_param::EChannelType::bgr:
                cv::cvtColor(input, output, cv::COLOR_RGB2BGR);
                break;
            case lyn_param::EChannelType::rgb:
            default:
                output = input.clone();
                break;
        }
    }
    return output;
}

void pre_process_thread(std::uint32_t                        chip_id,
                        const Model&                         model,
                        const cv::Mat&                       origin,
                        Cutter&                              cutter,
                        Queue<std::shared_ptr<Image4Infer>>& q_image) {
    Environment::GetInstance()->setContext(chip_id);
    const auto   tensor_input = model.get_model_desc().input.at(0);
    const size_t model_width  = tensor_input.dims.at(2);
    const size_t model_height = tensor_input.dims.at(1);
    ImageInfo    info{};
    while (cutter.get_next(info)) {
        const cv::Mat roi         = origin(cv::Rect(
            static_cast<int>(info.x_offset), static_cast<int>(info.y_offset),
            static_cast<int>(model_width), static_cast<int>(model_height)));
        const cv::Mat model_input = make_model_input(roi);
        q_image.wait();
        std::shared_ptr<DataPool<LynData>> in_pool(
            new DataPool<LynData>(2, model.get_inputs_size(), 1));
        std::shared_ptr<Image4Infer> img(
            new Image4Infer{in_pool->get(), ImageInfo()},
            [in_pool](Image4Infer* value) {
                in_pool->put(value->imgData);
                delete value;
            });
        img->imgInfo = info;
        const int input_channel =
            lyn_param::kYoloParam.type == lyn_param::EChannelType::gray ? 1 : 3;
        lynMemcpy(
            img->imgData.pointer(), model_input.ptr(),
            model_width * model_height * static_cast<size_t>(input_channel),
            ClientToServer);
        q_image.put(img);
    }
}

void infer_thread(std::uint32_t                        chip_id,
                  const Model&                         model,
                  Stream*                              send_stream,
                  Stream*                              recv_stream,
                  Queue<std::shared_ptr<Image4Infer>>& q_image,
                  Queue<std::shared_ptr<InferRes>>&    q_infer,
                  bool&                                pre_exit,
                  std::mutex&                          pre_mutex) {
    Environment::GetInstance()->setContext(chip_id);
    const int         batch_size = static_cast<int>(model.get_batch_size());
    DataPool<LynData> out_pool(2, model.get_outputs_size(),
                               static_cast<size_t>(batch_size));
    std::shared_ptr<DataPool<CPUData>> infer_pool(new DataPool<CPUData>(
        20, model.get_outputs_size(), static_cast<size_t>(batch_size)));
    while (true) {
        {
            std::lock_guard<std::mutex> lock(pre_mutex);
            if (q_image.size() == 0U && pre_exit) { break; }
        }
        std::shared_ptr<Image4Infer> img;
        try {
            img = q_image.get();
        } catch (const QueueClosed&) { break; }
        LynData model_out = out_pool.get();
        model.predict(*send_stream, *recv_stream, img->imgData, model_out);
        send_stream->wait();
        recv_stream->wait();
        std::shared_ptr<InferRes> infer_result(
            new InferRes{infer_pool->get(), img->imgInfo},
            [infer_pool](InferRes* value) {
                infer_pool->put(value->inferData);
                delete value;
            });
        model_out.read(infer_result->inferData);
        q_infer.put(infer_result);
        out_pool.put(model_out);
    }
}

std::vector<Detection> post_process_one(InferRes* infer_result,
                                        int       model_w,
                                        int       model_h) {
    YoloPostProcessInfo_t post_info{};
    post_info.class_num       = lyn_param::kYoloParam.class_num;
    post_info.is_pad_resize   = 1;
    post_info.score_threshold = lyn_param::kYoloParam.score_threshold;
    post_info.nms_threshold   = lyn_param::kYoloParam.nms_threshold;
    post_info.nms_top_k       = lyn_param::kYoloParam.nms_top_k;
    post_info.width           = model_w;
    post_info.height          = model_h;
    post_info.ori_width       = model_w;
    post_info.ori_height      = model_h;
    post_info.output_tensor = infer_result->inferData.slice(0, 1).pointer();
    if (lyn_param::kYoloParam.yolo_type == lyn_param::EYoloType::yolov8) {
        return Yolov8sPostProcess(&post_info);
    }
    if (lyn_param::kYoloParam.yolo_type == lyn_param::EYoloType::yolov9) {
        return Yolov9sPostProcess(&post_info);
    }
    return Yolov5sPostProcess(&post_info);
}

void post_process_thread(std::uint32_t                     chip_id,
                         const Model&                      model,
                         Queue<std::shared_ptr<InferRes>>& q_infer,
                         std::vector<Detection>&           detections,
                         bool&                             infer_exit,
                         std::mutex&                       infer_mutex) {
    (void)chip_id;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(infer_mutex);
            if (q_infer.size() == 0U && infer_exit) { break; }
        }
        std::shared_ptr<InferRes> infer_result;
        try {
            infer_result = q_infer.get();
        } catch (const QueueClosed&) { break; }
        const auto model_desc = model.get_model_desc();
        auto       tiles      = post_process_one(
            infer_result.get(), static_cast<int>(model_desc.input[0].dims[2]),
            static_cast<int>(model_desc.input[0].dims[1]));
        for (auto& item : tiles) {
            item.bbox.xmax +=
                static_cast<float>(infer_result->imgInfo.x_offset);
            item.bbox.xmin +=
                static_cast<float>(infer_result->imgInfo.x_offset);
            item.bbox.ymin +=
                static_cast<float>(infer_result->imgInfo.y_offset);
            item.bbox.ymax +=
                static_cast<float>(infer_result->imgInfo.y_offset);
        }
        detections.insert(detections.end(), tiles.begin(), tiles.end());
    }
}

void apply_nms(std::vector<Detection>& detections) {
    std::vector<Detection> filtered;
    yolo5Nms(detections, lyn_param::kYoloParam.nms_threshold,
             lyn_param::kYoloParam.nms_top_k, filtered, false);
    detections.swap(filtered);
}

void apply_config(const image_process::lynxi::YoloxRuntimeConfig& config) {
    lyn_param::kYoloParam.score_threshold = config.score_threshold;
    lyn_param::kYoloParam.nms_threshold   = config.nms_threshold;
    lyn_param::kYoloParam.nms_top_k       = config.nms_top_k;
    lyn_param::kYoloParam.class_num       = config.class_num;
    if (config.yolo_type == 2) {
        lyn_param::kYoloParam.yolo_type = lyn_param::EYoloType::yolov9;
    }
    else if (config.yolo_type == 0) {
        lyn_param::kYoloParam.yolo_type = lyn_param::EYoloType::yolov5;
    }
    else { lyn_param::kYoloParam.yolo_type = lyn_param::EYoloType::yolov8; }
    if (config.channel_type == 0) {
        lyn_param::kYoloParam.type = lyn_param::EChannelType::gray;
    }
    else if (config.channel_type == 2) {
        lyn_param::kYoloParam.type = lyn_param::EChannelType::bgr;
    }
    else { lyn_param::kYoloParam.type = lyn_param::EChannelType::rgb; }
}

}  // namespace

namespace image_process {
namespace lynxi {

struct YoloxRuntime::State {
    std::vector<Model*>  models;
    std::vector<Stream*> send_streams;
    std::vector<Stream*> recv_streams;
    std::uint32_t        device_id = 0;
};

YoloxRuntime::YoloxRuntime() = default;

YoloxRuntime::~YoloxRuntime() { stop(); }

void YoloxRuntime::configure(const YoloxRuntimeConfig& config) {
    config_ = config;
}

std::string resolve_model_path(const std::string& configured) {
    if (configured.empty()) { return {}; }
    std::filesystem::path path(configured);
    const auto            usable = [](const std::filesystem::path& candidate) {
        return std::filesystem::is_directory(candidate);
    };
    if (path.is_absolute() && usable(path)) { return path.string(); }
    const char* roots[] = {std::getenv("IMAGE_PROCESS_SOURCE_ROOT"),
                           std::getenv("IMAGE_PROCESS_DATA_ROOT"), nullptr};
    for (const char* root : roots) {
        if (root == nullptr || root[0] == '\0') { continue; }
        const std::filesystem::path candidate =
            std::filesystem::path(root) / path;
        if (usable(candidate)) { return candidate.string(); }
    }
    if (usable(path)) { return std::filesystem::absolute(path).string(); }
    return {};
}

bool YoloxRuntime::init_lyn(std::string& error) {
    stop();
    apply_config(config_);
    const std::string model_path = resolve_model_path(config_.model_path);
    if (model_path.empty()) {
        error = "Lynxi model directory is missing: " + config_.model_path;
        return false;
    }
    auto state                    = std::make_unique<State>();
    state->device_id              = config_.device_id;
    bool                init_ok   = true;
    const std::uint32_t device_id = config_.device_id;
    Environment::GetInstance()->createContext(
        device_id, [device_id, &init_ok](lynStream_t, ErrorMsg&&) {
            GST_ERROR("Lynxi stream error on device %u", device_id);
            init_ok = false;
        });
    try {
        Environment::GetInstance()->setContext(device_id);
        for (int i = 0; i < kModelCount; ++i) {
            state->models.push_back(new Model(model_path));
            state->send_streams.push_back(new Stream());
            state->recv_streams.push_back(new Stream());
        }
    } catch (const std::exception& exception) {
        error = exception.what();
        for (auto* stream : state->send_streams) { delete stream; }
        for (auto* stream : state->recv_streams) { delete stream; }
        for (auto* model : state->models) { delete model; }
        Environment::GetInstance()->destroyContext(device_id);
        return false;
    }
    if (!init_ok) {
        error = "Lynxi context callback failed during model load";
        return false;
    }
    state_ = std::move(state);
    ready_ = true;
    GST_INFO("Lynxi YOLO model loaded from %s on device %u", model_path.c_str(),
             device_id);
    return true;
}

bool YoloxRuntime::start(std::string& error) { return init_lyn(error); }

void YoloxRuntime::stop() {
    if (state_ == nullptr) {
        ready_ = false;
        return;
    }
    Environment::GetInstance()->setContext(state_->device_id);
    for (std::size_t i = 0; i < state_->send_streams.size(); ++i) {
        state_->send_streams[i]->wait_and_close();
        state_->recv_streams[i]->wait_and_close();
        delete state_->send_streams[i];
        delete state_->recv_streams[i];
    }
    for (auto* model : state_->models) { delete model; }
    Environment::GetInstance()->destroyContext(state_->device_id);
    state_.reset();
    ready_ = false;
}

bool YoloxRuntime::infer_gray8(const std::uint8_t*     data,
                               int                     width,
                               int                     height,
                               int                     stride,
                               std::vector<Detection>& out,
                               std::string&            error) {
    out.clear();
    if (!ready_) {
        error = "Lynxi detector is not started";
        return false;
    }
    if (state_ == nullptr || state_->models.empty()) {
        error = "Lynxi detector lost its model state";
        return false;
    }
    try {
        apply_config(config_);
        const auto tensor_input =
            state_->models[0]->get_model_desc().input.at(0);
        const size_t model_width = tensor_input.dims.at(2);
        if (width < static_cast<int>(model_width) ||
            height < static_cast<int>(model_width)) {
            error = "frame is smaller than the Lynxi model input";
            return false;
        }
        const size_t stride_px = static_cast<size_t>(
            std::round(static_cast<float>(model_width) * 0.78125F));
        Cutter  cutter(static_cast<size_t>(width), static_cast<size_t>(height),
                       model_width, stride_px);
        cv::Mat origin(height, width, CV_8UC1, const_cast<std::uint8_t*>(data),
                       static_cast<size_t>(stride));
        Queue<std::shared_ptr<Image4Infer>> q_image;
        Queue<std::shared_ptr<InferRes>>    q_infer;
        q_image.open_queue();
        q_infer.open_queue();
        bool                           pre_exit   = false;
        bool                           infer_exit = false;
        std::mutex                     pre_mutex;
        std::mutex                     infer_mutex;
        std::vector<std::future<void>> ipe;
        std::vector<std::future<void>> infer;
        std::vector<std::future<void>> post;
        for (int i = 0; i < kStreamNum; ++i) {
            ipe.emplace_back(std::async(
                std::launch::async, pre_process_thread, state_->device_id,
                std::ref(*state_->models[0]), std::ref(origin),
                std::ref(cutter), std::ref(q_image)));
        }
        for (int i = 0; i < kModelCount; ++i) {
            infer.emplace_back(std::async(
                std::launch::async, infer_thread, state_->device_id,
                std::ref(*state_->models[static_cast<std::size_t>(i)]),
                state_->send_streams[static_cast<std::size_t>(i)],
                state_->recv_streams[static_cast<std::size_t>(i)],
                std::ref(q_image), std::ref(q_infer), std::ref(pre_exit),
                std::ref(pre_mutex)));
        }
        post.emplace_back(std::async(
            std::launch::async, post_process_thread, state_->device_id,
            std::ref(*state_->models[0]), std::ref(q_infer), std::ref(out),
            std::ref(infer_exit), std::ref(infer_mutex)));
        for (auto& task : ipe) { task.wait(); }
        {
            std::lock_guard<std::mutex> lock(pre_mutex);
            pre_exit = true;
            if (q_image.size() == 0U) { q_image.close(); }
        }
        for (auto& task : infer) { task.wait(); }
        {
            std::lock_guard<std::mutex> lock(infer_mutex);
            infer_exit = true;
            if (q_infer.size() == 0U) { q_infer.close(); }
        }
        for (auto& task : post) { task.wait(); }
        apply_nms(out);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

}  // namespace lynxi
}  // namespace image_process
