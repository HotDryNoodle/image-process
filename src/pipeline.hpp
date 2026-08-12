#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace image_process {

using Json = nlohmann::json;

/** @brief A single raw frame and normalized metadata extracted from GStreamer.
 */
struct ProcessedFrame {
    std::vector<std::uint8_t> bytes;
    Json                      metadata;
};

/** @brief Pipeline execution output and runtime provenance. */
struct PipelineResult {
    std::size_t frame_count = 0;
    Json        first_frame_metadata;
    Json        last_frame_metadata;
    Json        provenance;
    Json        resource_usage;
};

/** @brief Receives one processed frame at a time while the pipeline runs. */
using FrameConsumer = std::function<void(const ProcessedFrame&)>;

/** @brief Receives concise normalized metadata without copying frame bytes. */
using MetadataConsumer = std::function<void(const Json&)>;

/** @brief Return a public pipeline plan with roles but no arbitrary launch
 * string. */
Json make_pipeline_plan(const Json& profile);

/** @brief Ensure every factory in the trusted profile exists in the registry.
 */
void preflight_pipeline(const Json& profile);

/**
 * @brief Run a trusted source-filter-appsink GStreamer pipeline.
 * @param profile Trusted registry entry.
 * @param input_path Existing raw input artifact path, or empty for fixtures.
 * @param max_frames Maximum number of frames accepted before fail-closed.
 * @param consumer Streaming consumer invoked once for every frame.
 * @return Frame counts, boundary metadata, provenance, and resource usage.
 */
PipelineResult run_pipeline(const Json&                  profile,
                            const std::filesystem::path& input_path,
                            std::size_t                  max_frames,
                            const FrameConsumer&         consumer);

/**
 * @brief Run the approved CDG0.0 to x264/MP4 host pipeline.
 * @param profile Trusted ground CDG0.0 registry entry.
 * @param input_path Existing CDG0.0 input file.
 * @param video_partial Partial MP4 path owned by the caller.
 * @param max_frames Maximum encoded input frames before fail-closed.
 * @param metadata_consumer Receives one concise metadata object per frame.
 * @return Frame count, provenance, and resource usage.
 */
PipelineResult run_ground_cdg00_pipeline(
    const Json&                  profile,
    const std::filesystem::path& input_path,
    const std::filesystem::path& video_partial,
    std::size_t                  max_frames,
    const MetadataConsumer&      metadata_consumer);

}  // namespace image_process
