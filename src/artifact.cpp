#include "artifact.hpp"

#include <fcntl.h>
#include <glib.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

#include "image_process.hpp"
#include "satellite/exit_codes.hpp"

namespace image_process {
namespace {

using TarEntries = std::map<std::string, std::string>;

void write_octal(std::array<char, 512>& header,
                 std::size_t            offset,
                 std::size_t            length,
                 std::uintmax_t         value) {
    std::array<char, 32> field{};
    const int written = std::snprintf(field.data(), field.size(), "%0*llo",
                                      static_cast<int>(length - 1),
                                      static_cast<unsigned long long>(value));
    if (written < 0 || static_cast<std::size_t>(written) >= length) {
        throw ImageProcessError(satellite::EXIT_FATAL,
                                "tar numeric field overflow");
    }
    std::memcpy(header.data() + offset, field.data(), length - 1);
    header[offset + length - 1] = '\0';
}

std::array<char, 512> make_tar_header(const std::string& name,
                                      std::size_t        size) {
    if (name.size() > 100U) {
        throw ImageProcessError(satellite::EXIT_FATAL,
                                "tar member path exceeds ustar limit: " + name);
    }
    std::array<char, 512> header{};
    std::memcpy(header.data(), name.data(), name.size());
    write_octal(header, 100, 8, 0644);
    write_octal(header, 108, 8, 0);
    write_octal(header, 116, 8, 0);
    write_octal(header, 124, 12, size);
    write_octal(header, 136, 12, 0);
    std::fill(header.begin() + 148, header.begin() + 156, ' ');
    header[156] = '0';
    std::memcpy(header.data() + 257, "ustar", 5);
    header[262] = '\0';
    header[263] = '0';
    header[264] = '0';

    unsigned int checksum = 0;
    for (const unsigned char byte : header) { checksum += byte; }
    std::array<char, 8> checksum_text{};
    std::snprintf(checksum_text.data(), checksum_text.size(), "%06o", checksum);
    std::memcpy(header.data() + 148, checksum_text.data(), 6);
    header[154] = '\0';
    header[155] = ' ';
    return header;
}

void append_entry(std::ofstream&     output,
                  const std::string& name,
                  const std::string& data) {
    const auto header = make_tar_header(name, data.size());
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    const std::size_t           padding = (512U - (data.size() % 512U)) % 512U;
    const std::array<char, 512> zeros{};
    output.write(zeros.data(), static_cast<std::streamsize>(padding));
}

TarEntries make_entries(const Json&                        manifest,
                        const std::vector<ProcessedFrame>& frames) {
    TarEntries entries;
    entries.emplace("manifest.json", manifest.dump(2) + "\n");
    std::ostringstream metadata;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        std::ostringstream name;
        name << "frames/" << std::setw(6) << std::setfill('0') << index
             << ".bin";
        entries.emplace(name.str(), std::string(reinterpret_cast<const char*>(
                                                    frames[index].bytes.data()),
                                                frames[index].bytes.size()));
        metadata << frames[index].metadata.dump() << '\n';
    }
    entries.emplace("meta/frames.jsonl", metadata.str());
    return entries;
}

}  // namespace

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ImageProcessError(
            satellite::EXIT_FATAL,
            "cannot open artifact for hashing: " + path.string());
    }
    GChecksum*                  checksum = g_checksum_new(G_CHECKSUM_SHA256);
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            g_checksum_update(checksum,
                              reinterpret_cast<const guchar*>(buffer.data()),
                              static_cast<gssize>(count));
        }
    }
    const std::string digest = g_checksum_get_string(checksum);
    g_checksum_free(checksum);
    return digest;
}

ProductArtifact write_product(const std::filesystem::path&       work_dir,
                              const Json&                        manifest,
                              const std::vector<ProcessedFrame>& frames) {
    std::filesystem::create_directories(work_dir);
    const std::filesystem::path partial = work_dir / "product.bin.partial";
    const std::filesystem::path product = work_dir / "product.bin";

    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw ImageProcessError(
            satellite::EXIT_FATAL,
            "cannot create product artifact: " + partial.string());
    }
    for (const auto& entry : make_entries(manifest, frames)) {
        append_entry(output, entry.first, entry.second);
    }
    const std::array<char, 1024> end_blocks{};
    output.write(end_blocks.data(),
                 static_cast<std::streamsize>(end_blocks.size()));
    output.flush();
    if (!output) {
        throw ImageProcessError(satellite::EXIT_FATAL,
                                "failed while writing product artifact");
    }
    output.close();

    const int file_descriptor = ::open(partial.c_str(), O_RDONLY);
    if (file_descriptor < 0 || ::fsync(file_descriptor) != 0) {
        if (file_descriptor >= 0) { ::close(file_descriptor); }
        throw ImageProcessError(satellite::EXIT_FATAL,
                                "failed to fsync product artifact");
    }
    ::close(file_descriptor);

    std::error_code error;
    std::filesystem::rename(partial, product, error);
    if (error) {
        throw ImageProcessError(
            satellite::EXIT_FATAL,
            "atomic product publication failed: " + error.message());
    }

    return {product, std::filesystem::file_size(product), sha256_file(product)};
}

}  // namespace image_process
