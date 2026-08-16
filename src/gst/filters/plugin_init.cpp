#include <gst/gst.h>

GType ip_image_filter_test_get_type(void);

namespace {

gboolean plugin_init(GstPlugin* plugin) {
    return gst_element_register(plugin, "ImageFilterTest", GST_RANK_NONE,
                                ip_image_filter_test_get_type());
}

}  // namespace

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  msffilters,
                  "image-process-owned compatibility filters",
                  plugin_init,
                  IP_RUNTIME_VERSION,
                  "MIT",
                  "image-process",
                  "https://github.com/HotDryNoodle/image-process")
