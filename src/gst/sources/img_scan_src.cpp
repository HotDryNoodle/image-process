/*
 * Portions refactored from MSF Project source at revision c4046d66.
 * Copyright (C) 2025 MSF Project.
 * See SOURCE_PROVENANCE.json and NOTICE for origin and license details.
 */
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "image_process/gst_meta_v1.h"

namespace {

struct ImageSourceState {
    std::filesystem::path              directory  = ".";
    bool                               recursive  = false;
    std::string                        extensions = "jpg,jpeg,png,bmp,tiff,pgm";
    int                                output_width  = 0;
    int                                output_height = 0;
    std::vector<std::filesystem::path> files;
    std::size_t                        next_file = 0;
};

GQuark state_quark() {
    return g_quark_from_static_string("image-process-img-scan-source-state");
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char byte) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(byte)));
    });
    return value;
}

std::set<std::string> parse_extensions(const std::string& value) {
    std::set<std::string> result;
    std::istringstream    stream(value);
    std::string           item;
    while (std::getline(stream, item, ',')) {
        const std::size_t first = item.find_first_not_of(" \t");
        const std::size_t last  = item.find_last_not_of(" \t");
        if (first == std::string::npos) { continue; }
        item = lowercase(item.substr(first, last - first + 1U));
        if (!item.empty() && item.front() != '.') { item.insert(0, "."); }
        result.insert(item);
    }
    return result;
}

void copy_text(char*              destination,
               std::size_t        capacity,
               const std::string& text) {
    if (capacity == 0U) { return; }
    const std::size_t count = std::min(capacity - 1U, text.size());
    std::memcpy(destination, text.data(), count);
    destination[count] = '\0';
}

}  // namespace

typedef struct _IpImgScanSrc {
    GstPushSrc parent;
} IpImgScanSrc;

typedef struct _IpImgScanSrcClass {
    GstPushSrcClass parent_class;
} IpImgScanSrcClass;

G_DEFINE_TYPE(IpImgScanSrc, ip_img_scan_src, GST_TYPE_PUSH_SRC)

namespace {

enum PropertyId {
    kPropertyNone,
    kPropertyDirectory,
    kPropertyRecursive,
    kPropertyExtensions,
    kPropertyOutputWidth,
    kPropertyOutputHeight,
};

ImageSourceState* state(IpImgScanSrc* source) {
    return static_cast<ImageSourceState*>(
        g_object_get_qdata(G_OBJECT(source), state_quark()));
}

void set_property(GObject*      object,
                  guint         property_id,
                  const GValue* value,
                  GParamSpec*   spec) {
    auto* value_state = state(reinterpret_cast<IpImgScanSrc*>(object));
    switch (property_id) {
        case kPropertyDirectory:
            value_state->directory = g_value_get_string(value) == nullptr
                                         ? "."
                                         : g_value_get_string(value);
            break;
        case kPropertyRecursive:
            value_state->recursive = g_value_get_boolean(value);
            break;
        case kPropertyExtensions:
            value_state->extensions = g_value_get_string(value) == nullptr
                                          ? ""
                                          : g_value_get_string(value);
            break;
        case kPropertyOutputWidth:
            value_state->output_width = g_value_get_int(value);
            break;
        case kPropertyOutputHeight:
            value_state->output_height = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

void get_property(GObject*    object,
                  guint       property_id,
                  GValue*     value,
                  GParamSpec* spec) {
    auto* value_state = state(reinterpret_cast<IpImgScanSrc*>(object));
    switch (property_id) {
        case kPropertyDirectory:
            g_value_set_string(value, value_state->directory.c_str());
            break;
        case kPropertyRecursive:
            g_value_set_boolean(value, value_state->recursive);
            break;
        case kPropertyExtensions:
            g_value_set_string(value, value_state->extensions.c_str());
            break;
        case kPropertyOutputWidth:
            g_value_set_int(value, value_state->output_width);
            break;
        case kPropertyOutputHeight:
            g_value_set_int(value, value_state->output_height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, spec);
    }
}

gboolean start(GstBaseSrc* base_source) {
    auto* source      = reinterpret_cast<IpImgScanSrc*>(base_source);
    auto* value_state = state(source);
    value_state->files.clear();
    value_state->next_file      = 0;
    const auto valid_extensions = parse_extensions(value_state->extensions);
    std::error_code error;
    if (!std::filesystem::is_directory(value_state->directory, error)) {
        GST_ELEMENT_ERROR(source, RESOURCE, NOT_FOUND,
                          ("image directory is not available"),
                          ("directory=%s", value_state->directory.c_str()));
        return FALSE;
    }

    auto append_if_supported =
        [&](const std::filesystem::directory_entry& item) {
            if (item.is_regular_file(error) &&
                valid_extensions.count(
                    lowercase(item.path().extension().string())) > 0U) {
                value_state->files.push_back(item.path());
            }
        };
    if (value_state->recursive) {
        for (std::filesystem::recursive_directory_iterator
                 iterator(value_state->directory, error),
             end;
             iterator != end && !error; iterator.increment(error)) {
            append_if_supported(*iterator);
        }
    }
    else {
        for (std::filesystem::directory_iterator
                 iterator(value_state->directory, error),
             end;
             iterator != end && !error; iterator.increment(error)) {
            append_if_supported(*iterator);
        }
    }
    if (error) {
        GST_ELEMENT_ERROR(
            source, RESOURCE, READ, ("failed to scan image directory"),
            ("directory=%s error=%s", value_state->directory.c_str(),
             error.message().c_str()));
        return FALSE;
    }
    std::sort(value_state->files.begin(), value_state->files.end());
    return TRUE;
}

gboolean stop(GstBaseSrc* base_source) {
    auto* value_state = state(reinterpret_cast<IpImgScanSrc*>(base_source));
    value_state->files.clear();
    value_state->next_file = 0;
    return TRUE;
}

GstFlowReturn create(GstPushSrc* push_source, GstBuffer** output_buffer) {
    auto* source      = reinterpret_cast<IpImgScanSrc*>(push_source);
    auto* value_state = state(source);
    if (value_state->next_file >= value_state->files.size()) {
        return GST_FLOW_EOS;
    }
    const std::filesystem::path file =
        value_state->files[value_state->next_file++];
    cv::Mat image = cv::imread(file.string(), cv::IMREAD_ANYCOLOR);
    if (image.empty()) {
        GST_ELEMENT_ERROR(source, RESOURCE, READ, ("failed to decode image"),
                          ("path=%s", file.c_str()));
        return GST_FLOW_ERROR;
    }
    const bool grayscale = image.channels() == 1;
    if (!grayscale) { cv::cvtColor(image, image, cv::COLOR_BGR2RGB); }
    if (value_state->output_width > 0 && value_state->output_height > 0) {
        cv::resize(
            image, image,
            cv::Size(value_state->output_width, value_state->output_height),
            0.0, 0.0, cv::INTER_LINEAR);
    }
    if (!image.isContinuous()) { image = image.clone(); }

    const std::size_t size   = image.total() * image.elemSize();
    GstBuffer*        buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (buffer == nullptr) { return GST_FLOW_ERROR; }
    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    std::memcpy(map.data, image.data, size);
    gst_buffer_unmap(buffer, &map);
    GST_BUFFER_PTS(buffer)      = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;

    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw", "format", G_TYPE_STRING, grayscale ? "GRAY8" : "RGB",
        "width", G_TYPE_INT, image.cols, "height", G_TYPE_INT, image.rows,
        "framerate", GST_TYPE_FRACTION, 0, 1, nullptr);
    const gboolean accepted = gst_pad_set_caps(GST_BASE_SRC_PAD(source), caps);
    gst_caps_unref(caps);
    if (!accepted) {
        gst_buffer_unref(buffer);
        return GST_FLOW_NOT_NEGOTIATED;
    }

    IpImageDirMetaV1 metadata{};
    metadata.abi_version = IP_GST_META_ABI_VERSION_V1;
    metadata.struct_size = sizeof(metadata);
    metadata.width       = static_cast<std::uint32_t>(image.cols);
    metadata.height      = static_cast<std::uint32_t>(image.rows);
    copy_text(metadata.absolute_path, sizeof(metadata.absolute_path),
              std::filesystem::absolute(file).string());
    copy_text(metadata.extension, sizeof(metadata.extension),
              lowercase(file.extension().string()));
    std::error_code relative_error;
    const auto      relative =
        std::filesystem::relative(file, value_state->directory, relative_error);
    copy_text(metadata.relative_path, sizeof(metadata.relative_path),
              relative_error ? file.filename().string() : relative.string());
    if (!ip_buffer_add_image_dir_meta(buffer, &metadata)) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    *output_buffer = buffer;
    return GST_FLOW_OK;
}

}  // namespace

static void ip_img_scan_src_class_init(IpImgScanSrcClass* source_class) {
    auto* object_class      = G_OBJECT_CLASS(source_class);
    auto* element_class     = GST_ELEMENT_CLASS(source_class);
    auto* base_source_class = GST_BASE_SRC_CLASS(source_class);
    auto* push_source_class = GST_PUSH_SRC_CLASS(source_class);

    object_class->set_property = set_property;
    object_class->get_property = get_property;
    g_object_class_install_property(
        object_class, kPropertyDirectory,
        g_param_spec_string("directory", "Directory", "Directory to scan", ".",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyRecursive,
        g_param_spec_boolean("recursive", "Recursive",
                             "Scan subdirectories recursively", FALSE,
                             static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyExtensions,
        g_param_spec_string("extensions", "Extensions",
                            "Comma-separated image extensions",
                            "jpg,jpeg,png,bmp,tiff,pgm",
                            static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyOutputWidth,
        g_param_spec_int("output-width", "Output width",
                         "Resize width; 0 preserves source width", 0, G_MAXINT,
                         0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, kPropertyOutputHeight,
        g_param_spec_int("output-height", "Output height",
                         "Resize height; 0 preserves source height", 0,
                         G_MAXINT, 0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        element_class, "Image Process Image Scan Source", "Source/Video/File",
        "Emits a deterministic, sorted image directory sequence",
        "image-process maintainers");
    static GstStaticPadTemplate source_template = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string){GRAY8,RGB}, "
                        "width=(int)[1,MAX], height=(int)[1,MAX], "
                        "framerate=(fraction)0/1"));
    gst_element_class_add_static_pad_template(element_class, &source_template);

    base_source_class->start  = start;
    base_source_class->stop   = stop;
    push_source_class->create = create;
}

static void ip_img_scan_src_init(IpImgScanSrc* source) {
    g_object_set_qdata_full(
        G_OBJECT(source), state_quark(), new ImageSourceState(),
        [](gpointer data) { delete static_cast<ImageSourceState*>(data); });
    gst_base_src_set_format(GST_BASE_SRC(source), GST_FORMAT_TIME);
}
