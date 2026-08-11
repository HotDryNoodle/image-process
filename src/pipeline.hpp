#pragma once

#include <cstdint>
#include <filesystem>
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
    std::vector<ProcessedFrame> frames;
    Json                        provenance;
};

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
 * @param max_frames Maximum frames retained in the result.
 */
PipelineResult run_pipeline(const Json&                  profile,
                            const std::filesystem::path& input_path,
                            std::size_t                  max_frames);

}  // namespace image_process
