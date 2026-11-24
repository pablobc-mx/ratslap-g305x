/* ratslap-g305x — HID++ 2.0 transport over Linux hidraw.
 *
 * Mirrors the framing of OpenGMice (GPL-3.0, github.com/sabam54/OpenGMice):
 * 20-byte long reports (report ID 0x11), software id 0x0B, feature-index
 * negotiation through the root feature (0x0000), replies validated against
 * device index / feature index / software id.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RSLAP_HIDPP_H
#define RSLAP_HIDPP_H

#include "util.h"

#define HIDPP_LONG_LEN 20
#define HIDPP_SW_ID 0x0B
#define HIDPP_MAX_PARAMS 16
#define RSLAP_VID_LOGITECH 0x046D

/* HID++ 2.0 features this tool uses. */
#define F_ROOT 0x0000
#define F_FEATURE_SET 0x0001
#define F_FW_VERSION 0x0003
#define F_DEVICE_NAME 0x0005
#define F_BATTERY_STATUS 0x1000
#define F_UNIFIED_BATTERY 0x1004
#define F_ADJUSTABLE_DPI 0x2201
#define F_EXTENDED_DPI 0x2202
#define F_REPORT_RATE 0x8060
#define F_EXT_REPORT_RATE 0x8061
#define F_ONBOARD_PROFILES 0x8100

/* ---------------------------------------------------------------- frames */

/* Build a long HID++ 2.0 request into out (needs >= HIDPP_LONG_LEN bytes):
 * [0x11, devnum, idx, (fn << 4) | SW_ID, params..., zero padding].
 * Returns frame length, or -1 if outlen/nparams are out of range. */
int hidpp_build_request(uint8_t *out, size_t outlen, uint8_t devnum, uint8_t idx,
                        uint8_t fn, const uint8_t *params, size_t nparams);

enum hidpp_reply_kind {
    HIDPP_REPLY_IGNORE = 0, /* not a valid HID++ frame */
    HIDPP_REPLY_DATA = 1,   /* [rep, dev, idx, fnsw, 16 params] */
    HIDPP_REPLY_ERR2 = 2,   /* HID++ 2.0 error: [rep, dev, 0xFF, idx, fnsw, code] */
    HIDPP_REPLY_ERR1 = 3,   /* HID++ 1.0 error: [rep, dev, 0x8F, idx, fnsw, code] */
};

/* Pure reply parse (no request matching). Fills the out pointers when
 * non-NULL; params_out receives the 16 reply parameter bytes (zero padded).
 * Returns one of enum hidpp_reply_kind. */
enum hidpp_reply_kind hidpp_classify(const uint8_t *buf, size_t len,
                                     uint8_t *devnum_out, uint8_t *idx_out,
                                     uint8_t *fnsw_out, uint8_t *params_out);

const char *hidpp_strerror(int code);

/* --------------------------------------------------------------- device */

/* One scan candidate: a hidraw node plus how to talk to it.
 * Direct USB/Bluetooth devices and hid-logitech-dj child nodes use
 * devnum 0xFF (replies may carry the receiver slot instead, so strict = 0);
 * a receiver node itself serves slots 1..6 and must match strictly. */
struct hidpp_candidate {
    char path[256];   /* e.g. /dev/hidraw0 (prefix overridable for tests) */
    char name[128];  /* HID_NAME, plus " slot N" for receiver slots */
    char link[24];   /* "USB", "wireless", "Bluetooth", "receiver" */
    int bus;         /* 3 = USB, 5 = Bluetooth */
    uint16_t vid, pid;
    int devnum;
    int strict;
};

/* Scan the hidraw class dir (RSLAP_SYSFS_PATH overrides /sys/class/hidraw,
 * RSLAP_DEV_PREFIX overrides /dev). Direct/dj nodes first, receiver slots
 * last. Returns the candidate count. */
int hidpp_scan(struct hidpp_candidate *out, int max);

/* Feature slots we cache indices for. */
enum {
    FEAT_FW,
    FEAT_NAME,
    FEAT_BATTERY,
    FEAT_UNIFIED_BATTERY,
    FEAT_DPI,
    FEAT_DPI_EXT,
    FEAT_RATE,
    FEAT_RATE_EXT,
    FEAT_ONBOARD,
    FEAT__COUNT
};

struct hidpp_err {
    int code;
    uint16_t feature;
    uint8_t fn;
};

struct hidpp_device {
    int fd;
    uint8_t devnum;
    int strict;
    int timeout_ms;
    struct hidpp_err err;
    uint16_t vid, pid;
    int bus;
    uint8_t idx[FEAT__COUNT];
    int have_idx[FEAT__COUNT];
};

/* Open + fill vid/pid from HIDIOCGRawInfo (falls back to the candidate).
 * Returns RSLAP_RC_OK or negative (RSLAP_RC_MALFORMED on open failure). */
int hidpp_open(struct hidpp_device *d, const struct hidpp_candidate *c);
void hidpp_close(struct hidpp_device *d);

/* Send one request to a feature index; returns RSLAP_RC_OK with the 16
 * reply parameter bytes in out, or a negative rslap_rc. */
int hidpp_request_idx(struct hidpp_device *d, uint8_t idx, uint8_t fn,
                      const uint8_t *params, size_t n,
                      uint8_t out[HIDPP_MAX_PARAMS]);

/* Ping feature index 0 (fn 1). Returns RSLAP_RC_OK, sets protocol bytes. */
int hidpp_ping(struct hidpp_device *d, uint8_t *proto_major, uint8_t *proto_minor);

/* Look up a feature index through root fn 0 and cache it in slot feat_slot.
 * Returns RSLAP_RC_OK or RSLAP_RC_UNSUPPORTED when the device lacks it. */
int hidpp_feature_index(struct hidpp_device *d, int feat_slot, uint16_t feature);

/* Request by feature slot (index must have been resolved before). */
int hidpp_request(struct hidpp_device *d, int feat_slot, uint8_t fn,
                  const uint8_t *params, size_t n,
                  uint8_t out[HIDPP_MAX_PARAMS]);

/* ---------------------------------------------------- feature conveniences */

int hidpp_get_name(struct hidpp_device *d, char *out, size_t outlen);
/* unit_out = 4 raw bytes of the per-device serial; models_out = up to 3
 * transport product ids (0 entries skipped). */
int hidpp_get_unit(struct hidpp_device *d, uint8_t unit_out[4], uint16_t models_out[3]);
int hidpp_get_firmware(struct hidpp_device *d, char *out, size_t outlen);
/* status_out receives a short static string ("Discharging", ...). */
int hidpp_get_battery(struct hidpp_device *d, int *pct, const char **status);
/* DPI list from 0x2202/0x2201 (empty slots NULL-terminated by count).
 * Returns the number of values, or negative rslap_rc. */
int hidpp_get_dpi_list(struct hidpp_device *d, int *vals, int max);
int hidpp_get_live_dpi(struct hidpp_device *d, int *x, int *y, uint8_t *lod);
/* NOTE: most mice refuse this in on-board mode with HID++ error 0x05;
 * callers must switch to host mode first (see README "DPI quirk"). */
int hidpp_set_live_dpi(struct hidpp_device *d, int x, int y, uint8_t lod);
int hidpp_get_live_rate(struct hidpp_device *d, int *hz);

#endif /* RSLAP_HIDPP_H */
