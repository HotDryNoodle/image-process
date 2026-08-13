#include "image_process/gst_meta_v1.h"

#include <gst/gst.h>

#include <cstring>

namespace {

gboolean alternate_init(GstMeta*, gpointer, GstBuffer*) { return TRUE; }

bool check(bool condition, const char* message) {
    if (!condition) { g_printerr("gst-meta-v1-test: %s\n", message); }
    return condition;
}

IpCdg00MetaV1 make_value() {
    IpCdg00MetaV1 value{};
    value.abi_version              = IP_GST_META_ABI_VERSION_V1;
    value.struct_size              = sizeof(value);
    value.window_start.struct_size = sizeof(value.window_start);
    value.window_end.struct_size   = sizeof(value.window_end);
    value.window_start.valid       = 1;
    value.window_start.gps_week    = 2300;
    value.window_start.gps_seconds = 42;
    value.window_start.lla[0]      = 120.5F;
    return value;
}

}  // namespace

int main(int argc, char** argv) {
    if (!gst_init_check(&argc, &argv, nullptr)) { return 1; }

    GstBuffer* buffer = gst_buffer_new();
    auto       input  = make_value();
    if (!check(ip_buffer_add_cdg00_meta(buffer, &input), "valid add failed")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    IpCdg00MetaV1 output{};
    if (!check(ip_buffer_get_cdg00_meta(buffer, &output), "valid get failed") ||
        !check(std::memcmp(&input, &output, sizeof(input)) == 0,
               "round trip changed value")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer = gst_buffer_new();
    input.struct_size -= 1U;
    if (!check(!ip_buffer_add_cdg00_meta(buffer, &input),
               "struct-size mismatch was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);

    buffer                       = gst_buffer_new();
    const GstMetaInfo* alternate = gst_meta_register(
        ip_cdg00_meta_api_get_type(), "AlternateCdg00MetaForContractTest",
        sizeof(GstMeta), alternate_init, nullptr, nullptr);
    if (!check(gst_buffer_add_meta(buffer, alternate, nullptr) != nullptr,
               "alternate meta setup failed") ||
        !check(!ip_buffer_get_cdg00_meta(buffer, &output),
               "foreign implementation with same API was accepted")) {
        gst_buffer_unref(buffer);
        return 1;
    }
    gst_buffer_unref(buffer);
    return 0;
}
