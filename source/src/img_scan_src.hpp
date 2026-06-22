/* MSF Image Scan Source
 * Copyright (C) 2025 MSF Project
 */

#ifndef __IMG_SCAN_SRC_HPP__
#define __IMG_SCAN_SRC_HPP__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_IMG_SCAN_SRC (img_scan_src_get_type())
G_DECLARE_FINAL_TYPE(ImgScanSrc, img_scan_src, GST, IMG_SCAN_SRC, GstPushSrc)

struct _ImgScanSrc {
    GstPushSrc parent;
    /* Private data is hidden in the implementation */
};

G_END_DECLS

#endif /* __IMG_SCAN_SRC_HPP__ */

