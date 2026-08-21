/**
 * @file yolox_runtime.hpp
 * @brief Single-chip Lynxi YOLO detector runtime used by
 * ImageProcessLynxiDetector.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "detect.h"

namespace image_process {
namespace lynxi {

/** @brief Installed-profile knobs for one Lynxi YOLO detector. */
struct YoloxRuntimeConfig {
    std::string   model_path;
    std::uint32_t device_id       = 0;
    float         score_threshold = 0.25F;
    float         nms_threshold   = 0.45F;
    int           nms_top_k       = 500;
    int           yolo_type       = 1;
    int           class_num       = 2;
    int           channel_type    = 1;
};

/**
 * @brief Loads one Lynxi model on one chip and infers GRAY8 frames.
 * @note This stage uses a single device_id. Dual-chip is out of scope.
 */
class YoloxRuntime {
  public:
    YoloxRuntime();
    ~YoloxRuntime();

    YoloxRuntime(const YoloxRuntime&)            = delete;
    YoloxRuntime& operator=(const YoloxRuntime&) = delete;

    /** @brief Replace runtime knobs. Does not load the model until start(). */
    void configure(const YoloxRuntimeConfig& config);

    /**
     * @brief Create Lynxi context, load model, and allocate streams.
     * @param error Human-readable failure when returning false.
     * @return true when the chip context and model are ready.
     */
    bool start(std::string& error);

    /** @brief Release streams, model, and device context. */
    void stop();

    /**
     * @brief Run tiled YOLO on one GRAY8 frame.
     * @param data Frame plane pointer.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param stride Plane stride in bytes.
     * @param out Detections in frame-pixel coordinates.
     * @param error Human-readable failure when returning false.
     */
    bool infer_gray8(const std::uint8_t*     data,
                     int                     width,
                     int                     height,
                     int                     stride,
                     std::vector<Detection>& out,
                     std::string&            error);

    /** @brief True after a successful start() that has not been stopped. */
    bool ready() const { return ready_; }

  private:
    bool init_lyn(std::string& error);

    struct State;
    YoloxRuntimeConfig     config_;
    std::unique_ptr<State> state_;
    bool                   ready_ = false;
};

/**
 * @brief Resolve an installed model directory.
 * @param configured Profile model-path, absolute or repo-relative.
 * @return Existing directory, or empty if none of the search roots contain it.
 */
std::string resolve_model_path(const std::string& configured);

}  // namespace lynxi
}  // namespace image_process
