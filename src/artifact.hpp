#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "pipeline.hpp"

namespace image_process {

using Json = nlohmann::json;

/** @brief Materialized product descriptor returned to the task manager. */
struct ProductArtifact {
    std::filesystem::path path;
    std::uintmax_t        size_bytes = 0;
    std::string           sha256;
};

/**
 * @brief Atomically publish a deterministic POSIX tar product bundle.
 * @param work_dir Task-owned output directory.
 * @param manifest Stable bundle manifest without wall-clock fields.
 * @param frames Processed raw frames and normalized metadata.
 */
ProductArtifact write_product(const std::filesystem::path&       work_dir,
                              const Json&                        manifest,
                              const std::vector<ProcessedFrame>& frames);

/** @brief Compute the SHA-256 digest of a file. */
std::string sha256_file(const std::filesystem::path& path);

}  // namespace image_process
