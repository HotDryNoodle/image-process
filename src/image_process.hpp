#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace image_process {

using Json = nlohmann::json;

/** @brief Validation result returned by the tools validate command. */
struct ValidationResult {
    bool        ok = false;
    std::string message;
    Json        details = Json::object();
};

/** @brief Typed plugin error carrying the satellite-plugin-sdk exit code. */
class ImageProcessError : public std::runtime_error {
  public:
    /** @brief Construct an error with a stable process exit code. */
    ImageProcessError(int exit_code, const std::string& message);

    /** @brief Return the process exit code associated with this error. */
    int exit_code() const noexcept;

  private:
    int exit_code_;
};

/** @brief Return the plugin manifest. */
Json make_manifest();

/** @brief Validate a request and its allowlisted runtime profile. */
ValidationResult validate_request(const Json& request);

/**
 * @brief Execute or preflight an allowlisted processing pipeline.
 * @param request Valid image processing request.
 * @param work_dir Writable task directory.
 * @param dry_run When true, return only the validated pipeline plan.
 */
Json run(const Json&                  request,
         const std::filesystem::path& work_dir,
         bool                         dry_run);

/** @brief Locate the trusted runtime profile registry. */
std::filesystem::path runtime_profiles_path();

}  // namespace image_process
