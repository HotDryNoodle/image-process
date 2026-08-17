#include <gst/gst.h>

GType ip_geometry_normalize_get_type(void);
GType ip_mock_detector_get_type(void);
GType ip_mock_tracker_get_type(void);
GType ip_text_sink_get_type(void);

namespace {

gboolean plugin_init(GstPlugin* plugin) {
    return gst_element_register(plugin, "ImageProcessGeometryNormalize",
                                GST_RANK_NONE,
                                ip_geometry_normalize_get_type()) &&
           gst_element_register(plugin, "ImageProcessMockDetector",
                                GST_RANK_NONE, ip_mock_detector_get_type()) &&
           gst_element_register(plugin, "ImageProcessMockTracker",
                                GST_RANK_NONE, ip_mock_tracker_get_type()) &&
           gst_element_register(plugin, "ImageProcessTextSink", GST_RANK_NONE,
                                ip_text_sink_get_type());
}

}  // namespace

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ipm4,
                  "image-process mock product elements",
                  plugin_init,
                  IP_RUNTIME_VERSION,
                  "MIT",
                  "image-process",
                  "https://github.com/HotDryNoodle/image-process")
