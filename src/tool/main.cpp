#include <iostream>
#include <string>

#include "image_process.hpp"
#include "satellite/cli_options.hpp"
#include "satellite/exit_codes.hpp"
#include "satellite/json_io.hpp"

namespace {

constexpr const char* kExecutable = "image-process";

void print_usage() {
    std::cerr << "image-process " << IMAGE_PROCESS_VERSION
              << " - controlled GStreamer image processing tools plugin\n\n"
              << "Usage:\n"
              << "  image-process manifest [--output json|json-pretty]\n"
              << "  " << satellite::plugin_cli_usage(kExecutable);
}

bool pretty_output(const satellite::PluginCliOptions& options) {
    return options.output == satellite::OutputFormat::JsonPretty;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return satellite::EXIT_USAGE;
    }
    const std::string command = argv[1];
    if (command == "--help" || command == "-h") {
        print_usage();
        return satellite::EXIT_OK;
    }
    if (command == "--version") {
        std::cout << kExecutable << ' ' << IMAGE_PROCESS_VERSION << '\n';
        return satellite::EXIT_OK;
    }

    try {
        if (command == "manifest") {
            bool pretty = false;
            if (argc == 4 && std::string(argv[2]) == "--output") {
                const std::string format = argv[3];
                if (format != "json" && format != "json-pretty") {
                    print_usage();
                    return satellite::EXIT_USAGE;
                }
                pretty = format == "json-pretty";
            }
            else if (argc != 2) {
                print_usage();
                return satellite::EXIT_USAGE;
            }
            satellite::write_json_stdout(image_process::make_manifest(),
                                         pretty);
            return satellite::EXIT_OK;
        }

        if (command != "validate" && command != "run") {
            print_usage();
            return satellite::EXIT_USAGE;
        }
        const bool require_work_dir = command == "run";
        const auto parsed =
            satellite::parse_plugin_cli(argc - 1, argv + 1, require_work_dir);
        if (!parsed.ok) {
            satellite::write_json_stdout(
                {{"ok", false}, {"error", parsed.error}}, false);
            return satellite::EXIT_USAGE;
        }
        if (parsed.options.show_help) {
            print_usage();
            return satellite::EXIT_OK;
        }
        const auto request = satellite::read_json_input(
            parsed.options.input_path, parsed.options.use_stdin);
        if (command == "validate") {
            const auto result = image_process::validate_request(request);
            satellite::write_json_stdout({{"ok", result.ok},
                                          {"message", result.message},
                                          {"details", result.details}},
                                         pretty_output(parsed.options));
            return result.ok ? satellite::EXIT_OK : satellite::EXIT_VALIDATION;
        }

        const auto output = image_process::run(
            request, *parsed.options.work_dir, parsed.options.dry_run);
        satellite::write_json_stdout(output, pretty_output(parsed.options));
        return satellite::EXIT_OK;
    } catch (const image_process::ImageProcessError& error) {
        satellite::write_json_stdout({{"ok", false}, {"error", error.what()}},
                                     false);
        return error.exit_code();
    } catch (const nlohmann::json::exception& error) {
        satellite::write_json_stdout({{"ok", false}, {"error", error.what()}},
                                     false);
        return satellite::EXIT_VALIDATION;
    } catch (const std::exception& error) {
        satellite::write_json_stdout({{"ok", false}, {"error", error.what()}},
                                     false);
        return satellite::EXIT_FATAL;
    }
}
