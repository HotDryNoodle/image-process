#include "image_process.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <vector>

#include "artifact.hpp"
#include "pipeline.hpp"
#include "satellite/exit_codes.hpp"
#include "satellite/json_io.hpp"

#ifndef IMAGE_PROCESS_SOURCE_ROOT
#define IMAGE_PROCESS_SOURCE_ROOT "."
#endif

#ifndef IMAGE_PROCESS_VERSION
#define IMAGE_PROCESS_VERSION "0.1.0"
#endif

namespace image_process {
namespace {

Json load_profiles() {
    return satellite::read_json_file(runtime_profiles_path());
}

const Json& require_object_member(const Json&        object,
                                  const std::string& name,
                                  ValidationResult&  result) {
    static const Json empty = Json::object();
    if (!object.contains(name) || !object.at(name).is_object()) {
        result.message = name + " must be an object";
        return empty;
    }
    return object.at(name);
}

bool has_only_keys(const Json&                  object,
                   const std::set<std::string>& allowed,
                   std::string&                 unexpected) {
    for (const auto& item : object.items()) {
        if (allowed.count(item.key()) == 0U) {
            unexpected = item.key();
            return false;
        }
    }
    return true;
}

std::string require_string(const Json&        object,
                           const std::string& name,
                           ValidationResult&  result) {
    if (!object.contains(name) || !object.at(name).is_string() ||
        object.at(name).get_ref<const std::string&>().empty()) {
        result.message = name + " must be a non-empty string";
        return {};
    }
    return object.at(name).get<std::string>();
}

Json sanitized_input_artifact(const Json& input) {
    Json sanitized = input;
    if (sanitized.contains("path")) {
        sanitized["basename"] =
            std::filesystem::path(sanitized.at("path").get<std::string>())
                .filename()
                .string();
        sanitized.erase("path");
    }
    return sanitized;
}

class GroundCdg00Collector {
  public:
    explicit GroundCdg00Collector(const std::filesystem::path& work_dir)
        : video_partial_(work_dir / "video.ogv.partial"),
          video_final_(work_dir / "video.ogv"),
          metadata_partial_(work_dir / "meta" / "cdg00.jsonl.partial"),
          metadata_final_(work_dir / "meta" / "cdg00.jsonl") {
        std::filesystem::create_directories(work_dir / "meta");
        metadata_output_.open(metadata_partial_,
                              std::ios::binary | std::ios::trunc);
        if (!metadata_output_) {
            throw ImageProcessError(satellite::EXIT_FATAL,
                                    "cannot create ground CDG0.0 outputs");
        }
    }

    GroundCdg00Collector(const GroundCdg00Collector&)            = delete;
    GroundCdg00Collector& operator=(const GroundCdg00Collector&) = delete;

    ~GroundCdg00Collector() {
        metadata_output_.close();
        if (!published_) {
            std::error_code ignored;
            std::filesystem::remove(video_partial_, ignored);
            std::filesystem::remove(metadata_partial_, ignored);
        }
    }

    void consume(const Json& metadata) {
        const std::set<std::string> expected = {"frame_id", "gps_time", "lla",
                                                "rpy", "velocity"};
        std::string                 unexpected;
        if (!metadata.is_object() ||
            !has_only_keys(metadata, expected, unexpected) ||
            metadata.size() != expected.size()) {
            throw ImageProcessError(
                satellite::EXIT_FATAL,
                "ground CDG0.0 metadata violates the concise contract");
        }
        if (metadata.at("frame_id").get<std::size_t>() != frame_count_) {
            throw ImageProcessError(satellite::EXIT_FATAL,
                                    "ground CDG0.0 metadata frame sequence is "
                                    "not contiguous");
        }
        metadata_output_ << metadata.dump() << '\n';
        if (!metadata_output_) {
            throw ImageProcessError(
                satellite::EXIT_FATAL,
                "failed while streaming ground CDG0.0 outputs");
        }
        ++frame_count_;
    }

    Json publish() {
        metadata_output_.flush();
        if (!metadata_output_) {
            throw ImageProcessError(satellite::EXIT_FATAL,
                                    "failed to flush ground CDG0.0 outputs");
        }
        metadata_output_.close();
        try {
            publish_partial_file(metadata_partial_, metadata_final_);
            publish_partial_file(video_partial_, video_final_);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(metadata_partial_, ignored);
            std::filesystem::remove(video_partial_, ignored);
            std::filesystem::remove(metadata_final_, ignored);
            std::filesystem::remove(video_final_, ignored);
            throw;
        }
        published_ = true;

        return {{"video",
                 {{"path", "video.ogv"},
                  {"media_type", "video/ogg;codecs=theora"},
                  {"size_bytes", std::filesystem::file_size(video_final_)},
                  {"sha256", sha256_file(video_final_)}}},
                {"metadata",
                 {{"path", "meta/cdg00.jsonl"},
                  {"media_type", "application/x-ndjson"},
                  {"size_bytes", std::filesystem::file_size(metadata_final_)},
                  {"sha256", sha256_file(metadata_final_)}}}};
    }

    std::size_t frame_count() const { return frame_count_; }

    const std::filesystem::path& video_partial_path() const {
        return video_partial_;
    }

  private:
    std::filesystem::path video_partial_;
    std::filesystem::path video_final_;
    std::filesystem::path metadata_partial_;
    std::filesystem::path metadata_final_;
    std::ofstream         metadata_output_;
    std::size_t           frame_count_ = 0;
    bool                  published_   = false;
};

std::uintmax_t verify_input_artifact(const Json&                  input,
                                     const std::filesystem::path& input_path,
                                     bool expect_directory) {
    std::error_code error;
    const auto      status = std::filesystem::symlink_status(input_path, error);
    if (std::filesystem::is_symlink(status)) {
        throw ImageProcessError(satellite::EXIT_VALIDATION,
                                "input artifact path must not be a symlink");
    }
    if (expect_directory) {
        if (error || !std::filesystem::is_directory(status)) {
            throw ImageProcessError(
                satellite::EXIT_VALIDATION,
                "image-sequence input artifact must be a directory");
        }
        if (input.contains("size_bytes") || input.contains("sha256")) {
            throw ImageProcessError(
                satellite::EXIT_VALIDATION,
                "image-sequence directory does not accept file digest fields");
        }
        std::filesystem::recursive_directory_iterator       iterator(input_path,
                                                                     error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) {
            throw ImageProcessError(
                satellite::EXIT_VALIDATION,
                "image-sequence directory cannot be inspected");
        }
        std::uintmax_t input_bytes = 0U;
        for (; iterator != end;) {
            const auto entry_status = iterator->symlink_status(error);
            if (error || std::filesystem::is_symlink(entry_status) ||
                (!std::filesystem::is_directory(entry_status) &&
                 !std::filesystem::is_regular_file(entry_status))) {
                throw ImageProcessError(
                    satellite::EXIT_VALIDATION,
                    "image-sequence directory contains an unsupported entry");
            }
            if (std::filesystem::is_regular_file(entry_status)) {
                const std::uintmax_t entry_size =
                    std::filesystem::file_size(iterator->path(), error);
                if (error) {
                    throw ImageProcessError(
                        satellite::EXIT_VALIDATION,
                        "image-sequence entry size cannot be inspected");
                }
                if (entry_size >
                    std::numeric_limits<std::uintmax_t>::max() - input_bytes) {
                    throw ImageProcessError(
                        satellite::EXIT_VALIDATION,
                        "image-sequence input size exceeds supported range");
                }
                input_bytes += entry_size;
            }
            iterator.increment(error);
            if (error) {
                throw ImageProcessError(
                    satellite::EXIT_VALIDATION,
                    "image-sequence directory cannot be inspected");
            }
        }
        return input_bytes;
    }
    if (error || !std::filesystem::is_regular_file(status)) {
        throw ImageProcessError(satellite::EXIT_VALIDATION,
                                "input artifact must be a regular file");
    }
    const std::uintmax_t actual_size = std::filesystem::file_size(input_path);
    if (input.contains("size_bytes") &&
        input.at("size_bytes").get<std::uintmax_t>() != actual_size) {
        throw ImageProcessError(satellite::EXIT_VALIDATION,
                                "input artifact size does not match request");
    }
    if (input.contains("sha256") &&
        input.at("sha256").get<std::string>() != sha256_file(input_path)) {
        throw ImageProcessError(satellite::EXIT_VALIDATION,
                                "input artifact digest does not match request");
    }
    return actual_size;
}

}  // namespace

ImageProcessError::ImageProcessError(int exit_code, const std::string& message)
    : std::runtime_error(message), exit_code_(exit_code) {}

int ImageProcessError::exit_code() const noexcept { return exit_code_; }

Json make_manifest() {
    return {
        {"schema_version", "1.1"},
        {"name", "image.process"},
        {"executable", "image-process"},
        {"version", IMAGE_PROCESS_VERSION},
        {"description",
         "Process existing image artifacts with allowlisted GStreamer "
         "pipelines"},
        {"domain", "image"},
        {"safety_class", "planning_only"},
        {"commands", {"manifest", "validate", "run"}},
        {"scenarios", {"pushbroom_detection", "stare_tracking"}},
        {"input_schema_path", "schemas/image_process.input.schema.json"},
        {"output_schema_path", "schemas/image_process.output.schema.json"},
        {"capabilities",
         {{"kind", "compute"},
          {"side_effect_class", "none"},
          {"relocatable", true},
          {"deterministic", false},
          {"idempotent", true},
          {"retryable", true},
          {"consumes", {"RawImageArtifact"}},
          {"produces", {"ProcessedImageArtifact", "ImageMetaSet"}},
          {"hardware_tag", "image.gstreamer"},
          {"timeout_sec", 300},
          {"compensation", nullptr},
          {"cost_hint", {{"typical_latency_sec", 30}}},
          {"async", false},
          {"dry_run", true},
          {"cancel", "process_signal"},
          {"requires_gmat", false},
          {"requires_hardware", false},
          {"batch", true}}},
        {"resource_limits",
         {{"timeout_sec", 300},
          {"max_parallel", 1},
          {"max_work_dir_mb", 4096}}},
        {"agent_hints",
         {{"when_to_use",
           "Run detection for pushbroom imagery or tracking for stare imagery "
           "after raw artifact capture"},
          {"prerequisites",
           {"raw_image_artifact_available", "runtime_profile_installed"}},
          {"typical_latency_sec", 30},
          {"compose_with", {"access.remote_sensing_access"}}}},
    };
}

std::filesystem::path runtime_profiles_path() {
    if (const char* data_root = std::getenv("IMAGE_PROCESS_DATA_ROOT")) {
        return std::filesystem::path(data_root) / "image-process" / "runtime" /
               "profiles.json";
    }
    if (const char* source_root = std::getenv("IMAGE_PROCESS_SOURCE_ROOT")) {
        return std::filesystem::path(source_root) / "configs" / "runtime" /
               "profiles.json";
    }
    return std::filesystem::path(IMAGE_PROCESS_SOURCE_ROOT) / "configs" /
           "runtime" / "profiles.json";
}

ValidationResult validate_request(const Json& request) {
    ValidationResult result;
    result.details = Json::object();
    if (!request.is_object()) {
        result.message = "request must be an object";
        return result;
    }

    std::string unexpected;
    if (!has_only_keys(request,
                       {"schema_version", "request_id", "trace_id",
                        "acquisition_mode", "sensor", "input", "processing"},
                       unexpected)) {
        result.message = "unsupported request field: " + unexpected;
        return result;
    }

    if (require_string(request, "schema_version", result) != "1.0") {
        if (result.message.empty()) {
            result.message = "schema_version must equal 1.0";
        }
        return result;
    }
    if (require_string(request, "request_id", result).empty()) {
        return result;
    }
    if (request.contains("trace_id") &&
        (!request.at("trace_id").is_string() ||
         request.at("trace_id").get_ref<const std::string&>().empty())) {
        result.message = "trace_id must be a non-empty string when present";
        return result;
    }
    const std::string mode =
        require_string(request, "acquisition_mode", result);
    if (mode != "pushbroom" && mode != "stare") {
        result.message = "acquisition_mode must be pushbroom or stare";
        return result;
    }

    const Json& sensor = require_object_member(request, "sensor", result);
    if (!result.message.empty()) { return result; }
    if (!has_only_keys(sensor, {"id"}, unexpected)) {
        result.message = "unsupported sensor field: " + unexpected;
        return result;
    }
    const std::string sensor_id = require_string(sensor, "id", result);
    if (sensor_id.empty()) { return result; }

    const Json& input = require_object_member(request, "input", result);
    if (!result.message.empty()) { return result; }
    if (!has_only_keys(
            input,
            {"artifact_ref", "path", "media_type", "size_bytes", "sha256"},
            unexpected)) {
        result.message = "unsupported input field: " + unexpected;
        return result;
    }
    if (require_string(input, "artifact_ref", result).empty() ||
        require_string(input, "media_type", result).empty()) {
        return result;
    }
    if (input.contains("size_bytes") &&
        (!input.at("size_bytes").is_number_integer() ||
         input.at("size_bytes").get<long long>() < 0)) {
        result.message = "input.size_bytes must be a non-negative integer";
        return result;
    }
    if (input.contains("sha256")) {
        if (!input.at("sha256").is_string()) {
            result.message = "input.sha256 must be a lowercase hex string";
            return result;
        }
        const std::string digest = input.at("sha256").get<std::string>();
        if (digest.size() != 64U ||
            digest.find_first_not_of("0123456789abcdef") != std::string::npos) {
            result.message =
                "input.sha256 must contain 64 lowercase hex digits";
            return result;
        }
    }

    const Json& processing =
        require_object_member(request, "processing", result);
    if (!result.message.empty()) { return result; }
    if (!has_only_keys(processing, {"runtime_profile", "max_frames"},
                       unexpected)) {
        result.message = "unsupported processing field: " + unexpected;
        return result;
    }
    const std::string profile_id =
        require_string(processing, "runtime_profile", result);
    if (profile_id.empty()) { return result; }

    Json registry;
    try {
        registry = load_profiles();
    } catch (const std::exception& error) {
        result.message = std::string("runtime profile registry unavailable: ") +
                         error.what();
        return result;
    }
    if (!registry.contains("profiles") ||
        !registry.at("profiles").contains(profile_id)) {
        result.message = "runtime_profile is not installed: " + profile_id;
        return result;
    }

    const Json& profile = registry.at("profiles").at(profile_id);
    if (profile.value("sensor_id", "") != sensor_id) {
        result.message = "runtime_profile does not match sensor.id";
        return result;
    }
    if (profile.value("acquisition_mode", "") != mode) {
        result.message = "runtime_profile does not match acquisition_mode";
        return result;
    }

    const std::string role        = profile.at("filter").value("role", "");
    const std::string output_mode = profile.value("output_mode", "frames");
    if (output_mode == "ground_cdg00" && role != "parse") {
        result.message = "ground CDG0.0 profile requires the parse role";
        return result;
    }
    if (output_mode != "ground_cdg00" && mode == "pushbroom" &&
        role.find("tracking") != std::string::npos) {
        result.message = "pushbroom mode forbids tracking filters";
        return result;
    }
    if (output_mode != "ground_cdg00" && mode == "pushbroom" &&
        role.find("detection") == std::string::npos) {
        result.message = "pushbroom mode requires a detection filter";
        return result;
    }
    if (mode == "stare" && role != "tracking") {
        result.message = "stare mode requires a tracking filter";
        return result;
    }

    const bool fixture = profile.value("evidence_class", "") == "synthetic";
    if (!fixture) {
        if (!input.contains("path") || !input.at("path").is_string() ||
            input.at("path").get_ref<const std::string&>().empty()) {
            result.message = "input.path is required for non-fixture profiles";
            return result;
        }
    }
    if (output_mode == "ground_cdg00" &&
        (!input.contains("size_bytes") || !input.contains("sha256"))) {
        result.message = "ground CDG0.0 input requires size_bytes and sha256";
        return result;
    }

    if (processing.contains("max_frames")) {
        if (!processing.at("max_frames").is_number_unsigned() &&
            !processing.at("max_frames").is_number_integer()) {
            result.message = "processing.max_frames must be an integer";
            return result;
        }
        const auto requested = processing.at("max_frames").get<long long>();
        const auto allowed   = profile.value("max_frames", 0LL);
        if (requested < 1 || requested > allowed) {
            result.message =
                "processing.max_frames exceeds the installed profile limit";
            return result;
        }
    }

    result.ok      = true;
    result.message = "request is valid";
    result.details = {{"runtime_profile", profile_id},
                      {"sensor_id", sensor_id},
                      {"acquisition_mode", mode},
                      {"filter_role", role},
                      {"evidence_class", profile.value("evidence_class", "")}};
    return result;
}

Json run(const Json&                  request,
         const std::filesystem::path& work_dir,
         bool                         dry_run) {
    const ValidationResult validation = validate_request(request);
    if (!validation.ok) {
        throw ImageProcessError(satellite::EXIT_VALIDATION, validation.message);
    }

    const std::string profile_id =
        request.at("processing").at("runtime_profile").get<std::string>();
    const Json registry = load_profiles();
    const Json profile  = registry.at("profiles").at(profile_id);
    const Json plan     = make_pipeline_plan(profile);

    preflight_pipeline(profile);

    Json output = {{"schema_version", "1.0"},
                   {"status", dry_run ? "dry_run" : "completed"},
                   {"request_id", request.at("request_id")},
                   {"trace_id", request.value("trace_id", Json(nullptr))},
                   {"runtime_profile", profile_id},
                   {"pipeline_plan", plan}};
    if (dry_run) { return output; }

    std::filesystem::create_directories(work_dir);
    std::filesystem::path input_path;
    std::uintmax_t        input_bytes = 0U;
    if (request.at("input").contains("path")) {
        input_path = request.at("input").at("path").get<std::string>();
        const bool expect_directory =
            profile.at("source").at("factory") == "ImgScanSrc";
        input_bytes = verify_input_artifact(request.at("input"), input_path,
                                            expect_directory);
    }

    const std::size_t profile_limit =
        profile.at("max_frames").get<std::size_t>();
    const std::size_t max_frames =
        request.at("processing").value("max_frames", profile_limit);
    const bool ground_cdg00 =
        profile.value("output_mode", "") == "ground_cdg00";
    std::vector<ProcessedFrame>           frames;
    PipelineResult                        pipeline_result;
    std::unique_ptr<GroundCdg00Collector> ground_collector;
    if (ground_cdg00) {
        for (const auto& path :
             {work_dir / "video.ogv", work_dir / "video.ogv.partial",
              work_dir / "video.mp4", work_dir / "video.mp4.partial",
              work_dir / "meta" / "cdg00.jsonl",
              work_dir / "meta" / "cdg00.jsonl.partial",
              work_dir / "result.json", work_dir / "pipeline-plan.json"}) {
            if (std::filesystem::exists(path)) {
                throw ImageProcessError(
                    satellite::EXIT_VALIDATION,
                    "ground output path already exists: " + path.string());
            }
        }
        ground_collector = std::make_unique<GroundCdg00Collector>(work_dir);
        pipeline_result  = run_ground_cdg00_pipeline(
            profile, input_path, ground_collector->video_partial_path(),
            max_frames,
            [&](const Json& metadata) { ground_collector->consume(metadata); });
    }
    else {
        pipeline_result = run_pipeline(
            profile, input_path, max_frames,
            [&](const ProcessedFrame& frame) { frames.push_back(frame); });
    }
    if (pipeline_result.frame_count == 0U) {
        throw ImageProcessError(satellite::EXIT_NO_RESULT,
                                "pipeline reached EOS without frames");
    }

    Json resource_usage           = pipeline_result.resource_usage;
    resource_usage["input_bytes"] = input_bytes;
    if (ground_collector) {
        if (ground_collector->frame_count() != pipeline_result.frame_count) {
            throw ImageProcessError(
                satellite::EXIT_FATAL,
                "ground metadata count does not match encoded input frames");
        }
        const Json artifacts    = ground_collector->publish();
        output["artifact"]      = artifacts.at("video");
        output["meta_artifact"] = artifacts.at("metadata");
        resource_usage["video_output_bytes"] =
            output.at("artifact").at("size_bytes");
        resource_usage["metadata_output_bytes"] =
            output.at("meta_artifact").at("size_bytes");
        const double wall =
            resource_usage.at("wall_time_seconds").get<double>();
        resource_usage["frames_per_second"] =
            wall > 0.0 ? static_cast<double>(pipeline_result.frame_count) / wall
                       : 0.0;
    }
    else {
        const Json bundle_manifest = {
            {"schema_version", "1.0"},
            {"request_id", request.at("request_id")},
            {"trace_id", request.value("trace_id", Json(nullptr))},
            {"input_artifact", sanitized_input_artifact(request.at("input"))},
            {"runtime_profile", profile_id},
            {"acquisition_mode", request.at("acquisition_mode")},
            {"sensor_id", request.at("sensor").at("id")},
            {"pipeline_plan", plan},
            {"provenance", pipeline_result.provenance},
            {"first_frame_metadata", pipeline_result.first_frame_metadata},
            {"last_frame_metadata", pipeline_result.last_frame_metadata},
            {"frame_count", pipeline_result.frame_count}};
        const ProductArtifact artifact =
            write_product(work_dir, bundle_manifest, frames);
        output["artifact"] = {{"path", artifact.path.filename().string()},
                              {"media_type", "application/x-tar"},
                              {"size_bytes", artifact.size_bytes},
                              {"sha256", artifact.sha256}};
    }
    output["frame_count"]    = pipeline_result.frame_count;
    output["provenance"]     = pipeline_result.provenance;
    output["resource_usage"] = resource_usage;
    write_json_atomic(work_dir / "pipeline-plan.json", plan);
    write_json_atomic(work_dir / "result.json", output);
    return output;
}

}  // namespace image_process
