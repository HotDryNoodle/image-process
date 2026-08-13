#include "image_process/gst_meta_v1.h"

#include <cstring>

namespace {

struct Cdg00GstMeta {
    GstMeta       meta;
    IpCdg00MetaV1 value;
};

struct ImageDirGstMeta {
    GstMeta          meta;
    IpImageDirMetaV1 value;
};

static_assert(sizeof(IpCdg00SampleV1) == 68U,
              "IpCdg00SampleV1 ABI size changed");
static_assert(sizeof(IpCdg00MetaV1) == 144U, "IpCdg00MetaV1 ABI size changed");
static_assert(sizeof(IpImageDirMetaV1) == 2096U,
              "IpImageDirMetaV1 ABI size changed");

bool valid_sample(const IpCdg00SampleV1& sample) {
    return sample.struct_size == sizeof(IpCdg00SampleV1);
}

bool valid_cdg00(const IpCdg00MetaV1& value) {
    return value.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           value.struct_size == sizeof(IpCdg00MetaV1) &&
           valid_sample(value.window_start) && valid_sample(value.window_end);
}

bool valid_image_dir(const IpImageDirMetaV1& value) {
    return value.abi_version == IP_GST_META_ABI_VERSION_V1 &&
           value.struct_size == sizeof(IpImageDirMetaV1);
}

gboolean cdg00_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<Cdg00GstMeta*>(meta);
    std::memset(&typed->value, 0, sizeof(typed->value));
    return TRUE;
}

gboolean cdg00_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const Cdg00GstMeta*>(meta);
    return ip_buffer_add_cdg00_meta(destination, &typed->value);
}

gboolean image_dir_init(GstMeta* meta, gpointer, GstBuffer*) {
    auto* typed = reinterpret_cast<ImageDirGstMeta*>(meta);
    std::memset(&typed->value, 0, sizeof(typed->value));
    return TRUE;
}

gboolean image_dir_transform(
    GstBuffer* destination, GstMeta* meta, GstBuffer*, GQuark, gpointer) {
    const auto* typed = reinterpret_cast<const ImageDirGstMeta*>(meta);
    return ip_buffer_add_image_dir_meta(destination, &typed->value);
}

}  // namespace

GType ip_cdg00_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "cdg00", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_cdg00_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_cdg00_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_cdg00_meta_api_get_type(), "ImageProcessCdg00MetaV1",
            sizeof(Cdg00GstMeta), cdg00_init, nullptr, cdg00_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_cdg00_meta(GstBuffer*           buffer,
                                  const IpCdg00MetaV1* value) {
    if (buffer == nullptr || value == nullptr || !valid_cdg00(*value)) {
        return FALSE;
    }
    auto* meta = reinterpret_cast<Cdg00GstMeta*>(
        gst_buffer_add_meta(buffer, ip_cdg00_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->value = *value;
    return TRUE;
}

gboolean ip_buffer_get_cdg00_meta(const GstBuffer* buffer,
                                  IpCdg00MetaV1*   value) {
    if (buffer == nullptr || value == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_cdg00_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const Cdg00GstMeta*>(current);
        if (!valid_cdg00(meta->value)) { return FALSE; }
        *value = meta->value;
        return TRUE;
    }
    return FALSE;
}

GType ip_image_dir_meta_api_get_type(void) {
    static gsize type = 0;
    if (g_once_init_enter(&type)) {
        static const gchar* tags[] = {"image", "file", nullptr};
        const GType         registered =
            gst_meta_api_type_register("meta_image_dir_api", tags);
        g_once_init_leave(&type, registered);
    }
    return static_cast<GType>(type);
}

const GstMetaInfo* ip_image_dir_meta_get_info(void) {
    static gsize info = 0;
    if (g_once_init_enter(&info)) {
        const GstMetaInfo* registered = gst_meta_register(
            ip_image_dir_meta_api_get_type(), "ImageProcessImageDirMetaV1",
            sizeof(ImageDirGstMeta), image_dir_init, nullptr,
            image_dir_transform);
        g_once_init_leave(&info, reinterpret_cast<gsize>(registered));
    }
    return reinterpret_cast<const GstMetaInfo*>(info);
}

gboolean ip_buffer_add_image_dir_meta(GstBuffer*              buffer,
                                      const IpImageDirMetaV1* value) {
    if (buffer == nullptr || value == nullptr || !valid_image_dir(*value)) {
        return FALSE;
    }
    auto* meta = reinterpret_cast<ImageDirGstMeta*>(
        gst_buffer_add_meta(buffer, ip_image_dir_meta_get_info(), nullptr));
    if (meta == nullptr) { return FALSE; }
    meta->value = *value;
    return TRUE;
}

gboolean ip_buffer_get_image_dir_meta(const GstBuffer*  buffer,
                                      IpImageDirMetaV1* value) {
    if (buffer == nullptr || value == nullptr) { return FALSE; }
    gpointer state = nullptr;
    while (GstMeta* current = gst_buffer_iterate_meta(
               const_cast<GstBuffer*>(buffer), &state)) {
        if (current->info != ip_image_dir_meta_get_info()) { continue; }
        const auto* meta = reinterpret_cast<const ImageDirGstMeta*>(current);
        if (!valid_image_dir(meta->value)) { return FALSE; }
        *value = meta->value;
        return TRUE;
    }
    return FALSE;
}
