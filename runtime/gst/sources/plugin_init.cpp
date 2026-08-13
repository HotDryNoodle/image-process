#include <gst/gst.h>

GType ip_cdg00_src_get_type(void);
#if IP_HAVE_OPENCV
GType ip_img_scan_src_get_type(void);
#endif

namespace {

gboolean plugin_init(GstPlugin* plugin) {
    gboolean success = gst_element_register(plugin, "CDG00Src", GST_RANK_NONE,
                                            ip_cdg00_src_get_type());
#if IP_HAVE_OPENCV
    success =
        success && gst_element_register(plugin, "ImgScanSrc", GST_RANK_NONE,
                                        ip_img_scan_src_get_type());
#endif
    return success;
}

}  // namespace

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  msfsrc,
                  "image-process-owned source elements",
                  plugin_init,
                  IP_RUNTIME_VERSION,
                  "MIT",
                  "image-process",
                  "https://github.com/HotDryNoodle/image-process")
