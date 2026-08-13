/**
 * @file gst_meta_v1.h
 * @brief Stable C ABI for image-process-owned GStreamer metadata.
 */

#ifndef IMAGE_PROCESS_GST_META_V1_H
#define IMAGE_PROCESS_GST_META_V1_H

#include <gst/gst.h>
#include <stdint.h>

G_BEGIN_DECLS

#define IP_GST_META_ABI_VERSION_V1 UINT32_C(1)

/** @brief One CDG0.0 parameter sample in source units. */
typedef struct IpCdg00SampleV1 {
    uint32_t struct_size;
    uint8_t  valid;
    uint8_t  channel_id;
    uint8_t  strip_number;
    uint8_t  time_sync_status;
    uint32_t row_number;
    uint32_t camera_seconds;
    uint32_t camera_microseconds;
    uint32_t exposure_time_ns;
    uint16_t gps_week;
    uint16_t reserved_0;
    uint32_t gps_seconds;
    float    lla[3];
    float    velocity[3];
    float    attitude[3];
} IpCdg00SampleV1;

/** @brief Versioned CDG0.0 metadata carried by a GstBuffer. */
typedef struct IpCdg00MetaV1 {
    uint32_t        abi_version;
    uint32_t        struct_size;
    IpCdg00SampleV1 window_start;
    IpCdg00SampleV1 window_end;
} IpCdg00MetaV1;

/** @brief Versioned image-directory provenance for development fixtures. */
typedef struct IpImageDirMetaV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    char     absolute_path[1024];
    char     relative_path[1024];
    char     extension[32];
} IpImageDirMetaV1;

/**
 * @brief Return the registered CDG0.0 metadata API type.
 * @return GType whose stable name is `meta_cdg00_api`.
 */
GType ip_cdg00_meta_api_get_type(void);

/**
 * @brief Return the CDG0.0 GstMeta registration record.
 * @return Non-null process-lifetime metadata registration.
 */
const GstMetaInfo* ip_cdg00_meta_get_info(void);

/**
 * @brief Attach a validated CDG0.0 v1 value to a buffer.
 * @param buffer Writable destination buffer.
 * @param value Versioned value to copy.
 * @return TRUE on success; FALSE for null or ABI-incompatible input.
 */
gboolean ip_buffer_add_cdg00_meta(GstBuffer*           buffer,
                                  const IpCdg00MetaV1* value);

/**
 * @brief Copy CDG0.0 v1 metadata from a buffer.
 * @param buffer Source buffer.
 * @param value Caller-owned output initialized by this function.
 * @return TRUE when compatible metadata exists; otherwise FALSE.
 */
gboolean ip_buffer_get_cdg00_meta(const GstBuffer* buffer,
                                  IpCdg00MetaV1*   value);

/** @brief Return the image-directory metadata API type. */
GType ip_image_dir_meta_api_get_type(void);

/** @brief Return the image-directory GstMeta registration record. */
const GstMetaInfo* ip_image_dir_meta_get_info(void);

/** @brief Attach a validated image-directory v1 value to a buffer. */
gboolean ip_buffer_add_image_dir_meta(GstBuffer*              buffer,
                                      const IpImageDirMetaV1* value);

/** @brief Copy image-directory v1 metadata from a buffer. */
gboolean ip_buffer_get_image_dir_meta(const GstBuffer*  buffer,
                                      IpImageDirMetaV1* value);

G_END_DECLS

#endif
