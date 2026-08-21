#include <gst/gst.h>

GType ip_lynxi_detector_get_type(void);

namespace {

gboolean plugin_init(GstPlugin* plugin) {
    return gst_element_register(plugin, "ImageProcessLynxiDetector",
                                GST_RANK_NONE, ip_lynxi_detector_get_type());
}

}  // namespace

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ipnpu,
                  "image-process Lynxi NPU detector",
                  plugin_init,
                  IP_RUNTIME_VERSION,
                  "MIT",
                  "image-process",
                  "https://github.com/HotDryNoodle/image-process")
