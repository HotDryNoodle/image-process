/* MSF Image Scan Source Implementation
 * Copyright (C) 2025 MSF Project
 */

#include "img_scan_src.hpp"
#include "meta/meta_image_dir.hpp"
#include "meta/meta_factory.hpp"

#include <opencv2/opencv.hpp>
#include <experimental/filesystem>
#include <vector>
#include <string>
#include <set>
#include <algorithm>

namespace fs = std::experimental::filesystem;

GST_DEBUG_CATEGORY_STATIC(img_scan_src_debug);
#define GST_CAT_DEFAULT img_scan_src_debug

/* Properties */
enum {
    PROP_0,
    PROP_DIRECTORY,
    PROP_RECURSIVE,
    PROP_EXTENSIONS,
    PROP_OUTPUT_WIDTH,
    PROP_OUTPUT_HEIGHT
};

#define DEFAULT_DIRECTORY "."
#define DEFAULT_RECURSIVE FALSE
#define DEFAULT_EXTENSIONS "jpg,jpeg,png,bmp"
#define DEFAULT_OUTPUT_WIDTH 0
#define DEFAULT_OUTPUT_HEIGHT 0

/* Private structure */
struct ImgScanSrcPrivate {
    gchar *directory;
    gboolean recursive;
    gchar *extensions_str;
    
    // Internal state
    std::vector<std::string> files;
    size_t current_index;
    std::set<std::string> valid_extensions;
};

/* Define private access */
// Note: G_ADD_PRIVATE was added in GObject 2.38. Assuming it's available.
// But to use it cleanly with C++ struct (non-POD), we need to manage constructor/destructor manually
// or use a pointer to C++ object.
// For simplicity with GObject C scaffolding, we'll stick to manual management in init/finalize
// but since we use std::vector, we MUST call constructor/destructor.
// We'll implement a minimal C++ wrapper.

class ImgScanSrcImpl {
public:
    std::string directory = DEFAULT_DIRECTORY;
    bool recursive = DEFAULT_RECURSIVE;
    std::string extensions_str = DEFAULT_EXTENSIONS;
    int output_width = 0;
    int output_height = 0;

    std::vector<fs::path> file_list;
    size_t current_index = 0;
    std::set<std::string> valid_extensions;

    void ParseExtensions() {
        valid_extensions.clear();
        std::stringstream ss(extensions_str);
        std::string ext;
        while (std::getline(ss, ext, ',')) {
            // Trim whitespace
            ext.erase(0, ext.find_first_not_of(" \t"));
            ext.erase(ext.find_last_not_of(" \t") + 1);
            // Add dot if missing
            if (!ext.empty() && ext[0] != '.') {
                ext = "." + ext;
            }
            // To lower case
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            valid_extensions.insert(ext);
        }
    }

    void ScanDirectory() {
        file_list.clear();
        current_index = 0;
        
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            GST_ERROR("Directory does not exist: %s", directory.c_str());
            return;
        }

        try {
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                    if ( fs::is_regular_file(entry.status()) && IsValidExtension(entry.path())) {
                        file_list.push_back(entry.path());
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(directory)) {
                    if ( fs::is_regular_file(entry.status()) && IsValidExtension(entry.path())) {
                        file_list.push_back(entry.path());
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            GST_ERROR("Filesystem error: %s", e.what());
        }
        
        // Sort for deterministic order
        std::sort(file_list.begin(), file_list.end());
        
        GST_INFO("Scanned %zu images in %s", file_list.size(), directory.c_str());
    }

    bool IsValidExtension(const fs::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return valid_extensions.count(ext) > 0;
    }
};

// Global map to store impl pointers (simplified approach instead of G_ADD_PRIVATE with C++)
// Or better, store `ImgScanSrcImpl*` in `_ImgScanSrc` structure via a `gpointer impl`.
// I'll redefine the struct in C++ file locally to access the pointer if I hadn't defined it as opaque in header.
// Since header defines it opaque, I can define it here or use `g_object_set_qdata`?
// The header has `struct _ImgScanSrc { GstPushSrc parent; };`.
// I cannot extend it here easily without casting.
// I will use `g_object_set_qdata` to attach C++ impl, or just assume `_ImgScanSrc` has a `gpointer` member if I could edit header.
// I'll edit header to add `gpointer impl` for safety.

// Re-editing header in previous step would be better, but I can do it now.
// Actually, standard way for C++ GObject is using a Private struct in the instance struct.
// Let's edit the header to add `gpointer impl;` to `_ImgScanSrc`.

// Wait, I can't edit the header in the middle of this file writing.
// I will assume the header update happens. I will perform header update after this tool call or use `g_object_get_qdata` for now.
// `g_object_set_qdata` is safe.

static GQuark impl_quark() {
    static GQuark quark = g_quark_from_static_string("img-scan-src-impl");
    return quark;
}

static ImgScanSrcImpl* get_impl(ImgScanSrc* src) {
    return static_cast<ImgScanSrcImpl*>(g_object_get_qdata(G_OBJECT(src), impl_quark()));
}

static void create_impl(ImgScanSrc* src) {
    g_object_set_qdata_full(G_OBJECT(src), impl_quark(), new ImgScanSrcImpl(), [](gpointer data){
        delete static_cast<ImgScanSrcImpl*>(data);
    });
}

/* Forward declarations */
static void img_scan_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void img_scan_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean img_scan_src_start(GstBaseSrc *basesrc);
static gboolean img_scan_src_stop(GstBaseSrc *basesrc);
static GstFlowReturn img_scan_src_create(GstPushSrc *pushsrc, GstBuffer **buf);

G_DEFINE_TYPE_WITH_CODE(ImgScanSrc, img_scan_src, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT(img_scan_src_debug, "imgscansrc", 0, "Image Directory Scan Source"))

static void img_scan_src_class_init(ImgScanSrcClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS(klass);
    GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS(klass);

    gobject_class->set_property = img_scan_src_set_property;
    gobject_class->get_property = img_scan_src_get_property;

    g_object_class_install_property(gobject_class, PROP_DIRECTORY,
        g_param_spec_string("directory", "Directory", "Directory to scan",
            DEFAULT_DIRECTORY, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_RECURSIVE,
        g_param_spec_boolean("recursive", "Recursive", "Scan recursively",
            DEFAULT_RECURSIVE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_EXTENSIONS,
        g_param_spec_string("extensions", "Extensions", "Comma separated extensions",
            DEFAULT_EXTENSIONS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_OUTPUT_WIDTH,
        g_param_spec_int("output-width", "Output Width",
            "Resize all output images to this width (0 = keep original)",
            0, G_MAXINT, DEFAULT_OUTPUT_WIDTH,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_OUTPUT_HEIGHT,
        g_param_spec_int("output-height", "Output Height",
            "Resize all output images to this height (0 = keep original)",
            0, G_MAXINT, DEFAULT_OUTPUT_HEIGHT,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class,
        "Image Scan Source", "Source/Video", "Scans directory for images", "MSF Project");

    static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string){GRAY8, RGB}, width=[1,MAX], height=[1,MAX], framerate=0/1"));
    
    gst_element_class_add_static_pad_template(element_class, &src_template);

    basesrc_class->start = GST_DEBUG_FUNCPTR(img_scan_src_start);
    basesrc_class->stop = GST_DEBUG_FUNCPTR(img_scan_src_stop);
    pushsrc_class->create = GST_DEBUG_FUNCPTR(img_scan_src_create);
}

static void img_scan_src_init(ImgScanSrc *src) {
    create_impl(src);
    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);
}

static void img_scan_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    ImgScanSrc *src = GST_IMG_SCAN_SRC(object);
    ImgScanSrcImpl *impl = get_impl(src);

    switch (prop_id) {
        case PROP_DIRECTORY:
            impl->directory = g_value_get_string(value);
            break;
        case PROP_RECURSIVE:
            impl->recursive = g_value_get_boolean(value);
            break;
        case PROP_EXTENSIONS:
            impl->extensions_str = g_value_get_string(value);
            break;
        case PROP_OUTPUT_WIDTH:
            impl->output_width = g_value_get_int(value);
            break;
        case PROP_OUTPUT_HEIGHT:
            impl->output_height = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void img_scan_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    ImgScanSrc *src = GST_IMG_SCAN_SRC(object);
    ImgScanSrcImpl *impl = get_impl(src);

    switch (prop_id) {
        case PROP_DIRECTORY:
            g_value_set_string(value, impl->directory.c_str());
            break;
        case PROP_RECURSIVE:
            g_value_set_boolean(value, impl->recursive);
            break;
        case PROP_EXTENSIONS:
            g_value_set_string(value, impl->extensions_str.c_str());
            break;
        case PROP_OUTPUT_WIDTH:
            g_value_set_int(value, impl->output_width);
            break;
        case PROP_OUTPUT_HEIGHT:
            g_value_set_int(value, impl->output_height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static gboolean img_scan_src_start(GstBaseSrc *basesrc) {
    ImgScanSrc *src = GST_IMG_SCAN_SRC(basesrc);
    ImgScanSrcImpl *impl = get_impl(src);
    
    impl->ParseExtensions();
    impl->ScanDirectory();
    
    return TRUE;
}

static gboolean img_scan_src_stop(GstBaseSrc *basesrc) {
    ImgScanSrc *src = GST_IMG_SCAN_SRC(basesrc);
    ImgScanSrcImpl *impl = get_impl(src);
    
    impl->file_list.clear();
    impl->current_index = 0;
    
    return TRUE;
}

// 自己实现一个简化版 relative()
fs::path my_relative(const fs::path& p, const fs::path& base)
{
    // 1. 统一转成绝对路径
    fs::path abs_p    = fs::absolute(p);
    fs::path abs_base = fs::absolute(base);

    // 2. 逐个分量拆出来
    std::vector<fs::path> vp, vbase;
    for (auto& x : abs_p)    vp.push_back(x);
    for (auto& x : abs_base) vbase.push_back(x);

    // 3. 找公共前缀长度
    std::size_t common = 0;
    while (common < vp.size() && common < vbase.size() &&
           vp[common] == vbase[common])
        ++common;

    // 4. 剩余部分拼 ../
    fs::path rel;
    for (std::size_t i = common; i < vbase.size(); ++i)
        rel /= "..";

    // 5. 追加 p 的剩余部分
    for (std::size_t i = common; i < vp.size(); ++i)
        rel /= vp[i];

    return rel.empty() ? "." : rel;
}
static GstFlowReturn img_scan_src_create(GstPushSrc *pushsrc, GstBuffer **buf) {
    ImgScanSrc *src = GST_IMG_SCAN_SRC(pushsrc);
    ImgScanSrcImpl *impl = get_impl(src);
    
    if (impl->current_index >= impl->file_list.size()) {
        GST_INFO_OBJECT(src, "All files processed");
        return GST_FLOW_EOS;
    }
    
    fs::path current_file = impl->file_list[impl->current_index];
    impl->current_index++;
    
    // Read image (try as color first to detect format)
    cv::Mat img = cv::imread(current_file.string(), cv::IMREAD_ANYCOLOR);
    if (img.empty()) {
        GST_WARNING_OBJECT(src, "Failed to read image: %s", current_file.c_str());
        return GST_FLOW_ERROR;
    }

    // Detect image format based on channels
    bool is_grayscale = (img.channels() == 1);
    const char* format_str = is_grayscale ? "GRAY8" : "RGB";

    // Convert color format if needed (only for color images)
    if (!is_grayscale) {
        // Convert BGR to RGB (OpenCV reads as BGR, GStreamer expects RGB)
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
    }

    // Resize to fixed output dimensions if requested
    int out_width = img.cols;
    int out_height = img.rows;
    if (impl->output_width > 0 && impl->output_height > 0) {
        out_width = impl->output_width;
        out_height = impl->output_height;
        cv::Mat resized;
        cv::resize(img, resized, cv::Size(out_width, out_height), 0, 0, cv::INTER_LINEAR);
        img = std::move(resized);
    }

    // Create GstBuffer
    gsize size = img.total() * img.elemSize();
    *buf = gst_buffer_new_allocate(NULL, size, NULL);

    GstMapInfo map;
    gst_buffer_map(*buf, &map, GST_MAP_WRITE);
    std::memcpy(map.data, img.data, size);
    gst_buffer_unmap(*buf, &map);

    // Set buffer properties
    GST_BUFFER_PTS(*buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(*buf) = GST_CLOCK_TIME_NONE;

    // Set Caps dynamically based on image format
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, format_str,
        "width", G_TYPE_INT, out_width,
        "height", G_TYPE_INT, out_height,
        "framerate", GST_TYPE_FRACTION, 0, 1,
        NULL);
    gst_pad_set_caps(GST_BASE_SRC_PAD(src), caps);
    gst_caps_unref(caps);

    // Add ImageDirMeta
    msf::MetaImageDirImpl dir_meta;
    dir_meta.file_path = fs::absolute(current_file).string();

    // Compute relative path
    try {
        dir_meta.rel_path = my_relative(current_file, impl->directory).string();
    } catch (...) {
        dir_meta.rel_path = current_file.filename().string();
    }

    dir_meta.width = out_width;
    dir_meta.height = out_height;
    dir_meta.format = current_file.extension().string();
    if (!dir_meta.format.empty() && dir_meta.format[0] == '.') dir_meta.format.erase(0, 1);

    if (!msf::MetaFactory::AddMetaToBuffer(*buf, dir_meta)) {
        GST_WARNING_OBJECT(src, "Failed to add ImageDirMeta");
    }

    return GST_FLOW_OK;
}

