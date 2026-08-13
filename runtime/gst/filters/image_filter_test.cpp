#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

typedef struct _IpImageFilterTest {
    GstBaseTransform parent;
} IpImageFilterTest;

typedef struct _IpImageFilterTestClass {
    GstBaseTransformClass parent_class;
} IpImageFilterTestClass;

G_DEFINE_TYPE(IpImageFilterTest, ip_image_filter_test, GST_TYPE_BASE_TRANSFORM)

namespace {

GstFlowReturn transform_in_place(GstBaseTransform*, GstBuffer*) {
    return GST_FLOW_OK;
}

}  // namespace

static void ip_image_filter_test_class_init(
    IpImageFilterTestClass* filter_class) {
    auto* element_class   = GST_ELEMENT_CLASS(filter_class);
    auto* transform_class = GST_BASE_TRANSFORM_CLASS(filter_class);
    gst_element_class_set_static_metadata(
        element_class, "Image Process Development Filter", "Filter/Video",
        "Compatibility filter for deterministic development profiles",
        "image-process maintainers");
    static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string){GRAY8,RGB}, "
                        "width=(int)[1,MAX], height=(int)[1,MAX]"));
    static GstStaticPadTemplate source_template = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=(string){GRAY8,RGB}, "
                        "width=(int)[1,MAX], height=(int)[1,MAX]"));
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &source_template);
    transform_class->transform_ip = transform_in_place;
}

static void ip_image_filter_test_init(IpImageFilterTest* filter) {
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(filter), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(filter), TRUE);
}
