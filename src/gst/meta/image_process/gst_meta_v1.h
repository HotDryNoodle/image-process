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
#define IP_GST_META_ABI_NAME "image-process.gst-meta.v1"
#define IP_GST_META_MAX_TARGETS_PER_FRAME_V1 256U
#define IP_GST_META_GEOMETRY_ID_MAX_V1 64U

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

/** @brief Area-array frame parameters derived from a CDG0.0 sample. */
typedef struct IpAreaFrameMetaV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t  valid;
    uint8_t  channel_id;
    uint8_t  strip_number;
    uint8_t  time_sync_status;
    uint32_t row_number;
    uint32_t camera_seconds;
    uint32_t camera_microseconds;
    uint32_t exposure_time_ns;
} IpAreaFrameMetaV1;

/** @brief Axis-aligned original→filter map: x' = scale_x * x + offset_x. */
typedef struct IpScaleOffset2DV1 {
    uint32_t struct_size;
    uint32_t reserved_0;
    double   scale_x;
    double   scale_y;
    double   offset_x;
    double   offset_y;
} IpScaleOffset2DV1;

/** @brief Geometry stamped by ImageProcessGeometryNormalize. */
typedef struct IpGeometryMetaV1 {
    uint32_t          abi_version;
    uint32_t          struct_size;
    uint32_t          original_width;
    uint32_t          original_height;
    uint32_t          filter_width;
    uint32_t          filter_height;
    char              geometry_id[IP_GST_META_GEOMETRY_ID_MAX_V1];
    IpScaleOffset2DV1 map;
} IpGeometryMetaV1;

/** @brief Half-open bbox in filter-input continuous zero-origin coordinates. */
typedef struct IpBBoxV1 {
    uint32_t struct_size;
    uint32_t reserved_0;
    double   x_min;
    double   y_min;
    double   x_max;
    double   y_max;
} IpBBoxV1;

/** @brief One detection target in filter-input coordinates. */
typedef struct IpDetectionTargetV1 {
    uint32_t struct_size;
    uint32_t class_index;
    double   confidence;
    IpBBoxV1 bbox;
} IpDetectionTargetV1;

/** @brief Detection frame header. Count is exposed via accessors. */
typedef struct IpDetectionFrameV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t frame_id;
    uint32_t target_count;
} IpDetectionFrameV1;

/** @brief One tracked target in filter-input coordinates. */
typedef struct IpTrackingTargetV1 {
    uint32_t struct_size;
    uint32_t reserved_0;
    uint64_t track_id;
    uint32_t class_index;
    uint32_t reserved_1;
    IpBBoxV1 bbox;
} IpTrackingTargetV1;

/** @brief Tracking frame header. */
typedef struct IpTrackingFrameV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t frame_id;
    uint32_t tracked_target_count;
} IpTrackingFrameV1;

/** @brief Return the area-frame metadata API type. */
GType ip_area_frame_meta_api_get_type(void);
/** @brief Return the area-frame GstMeta registration record. */
const GstMetaInfo* ip_area_frame_meta_get_info(void);
/** @brief Attach a validated area-frame v1 value; rejects duplicates. */
gboolean ip_buffer_add_area_frame_meta(GstBuffer*               buffer,
                                       const IpAreaFrameMetaV1* value);
/** @brief Copy area-frame v1 metadata; rejects a foreign impl. */
gboolean ip_buffer_get_area_frame_meta(const GstBuffer*   buffer,
                                       IpAreaFrameMetaV1* value);

/** @brief Return the geometry metadata API type. */
GType ip_geometry_meta_api_get_type(void);
/** @brief Return the geometry GstMeta registration record. */
const GstMetaInfo* ip_geometry_meta_get_info(void);
/** @brief Attach a validated geometry v1 value; rejects duplicates. */
gboolean ip_buffer_add_geometry_meta(GstBuffer*              buffer,
                                     const IpGeometryMetaV1* value);
/** @brief Copy geometry v1 metadata; rejects a foreign impl. */
gboolean ip_buffer_get_geometry_meta(const GstBuffer*  buffer,
                                     IpGeometryMetaV1* value);

/** @brief Return the detection metadata API type. */
GType ip_detection_meta_api_get_type(void);
/** @brief Return the detection GstMeta registration record. */
const GstMetaInfo* ip_detection_meta_get_info(void);
/**
 * @brief Attach one detection frame and up to 256 targets.
 * @param targets May be NULL when frame->target_count is 0.
 */
gboolean ip_buffer_add_detection_meta(GstBuffer*                 buffer,
                                      const IpDetectionFrameV1*  frame,
                                      const IpDetectionTargetV1* targets);
/** @brief Copy the detection frame header. */
gboolean ip_buffer_get_detection_frame(const GstBuffer*    buffer,
                                       IpDetectionFrameV1* frame);
/** @brief Copy one detection target by index. */
gboolean ip_buffer_get_detection_target(const GstBuffer*     buffer,
                                        uint32_t             index,
                                        IpDetectionTargetV1* target);

/** @brief Return the tracking metadata API type. */
GType ip_tracking_meta_api_get_type(void);
/** @brief Return the tracking GstMeta registration record. */
const GstMetaInfo* ip_tracking_meta_get_info(void);
/**
 * @brief Attach one tracking frame and up to 256 targets.
 * @param targets May be NULL when frame->tracked_target_count is 0.
 */
gboolean ip_buffer_add_tracking_meta(GstBuffer*                buffer,
                                     const IpTrackingFrameV1*  frame,
                                     const IpTrackingTargetV1* targets);
/** @brief Copy the tracking frame header. */
gboolean ip_buffer_get_tracking_frame(const GstBuffer*   buffer,
                                      IpTrackingFrameV1* frame);
/** @brief Copy one tracking target by index. */
gboolean ip_buffer_get_tracking_target(const GstBuffer*    buffer,
                                       uint32_t            index,
                                       IpTrackingTargetV1* target);

G_END_DECLS

#endif
