#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
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

/**
 * @brief Atomically publish a deterministic product from existing files.
 * @param work_dir Task-owned output directory.
 * @param manifest Stable bundle manifest without wall-clock fields.
 * @param members Pairs of tar member name and source file path.
 * @return Published product descriptor.
 */
ProductArtifact write_product_from_files(
    const std::filesystem::path&                                      work_dir,
    const Json&                                                       manifest,
    const std::vector<std::pair<std::string, std::filesystem::path>>& members);

/**
 * @brief Fsync a partial file and atomically rename it to its final path.
 * @param partial Existing partial file.
 * @param final Final publication path.
 */
void publish_partial_file(const std::filesystem::path& partial,
                          const std::filesystem::path& final);

/** @brief Atomically write a JSON document with a trailing newline. */
void write_json_atomic(const std::filesystem::path& path, const Json& value);

/** @brief Compute the SHA-256 digest of a file. */
std::string sha256_file(const std::filesystem::path& path);

}  // namespace image_process
