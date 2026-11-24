/* stubs for the device-facing transport; frame helpers are real
 * (test_hidpp.c drove their implementation). */
#include "hidpp.h"
#include <string.h>

static const char *const hidpp_errors[] = {
    "no error", "unknown", "invalid argument", "out of range", "hardware error",
    "logitech internal", "invalid feature index", "invalid function", "busy",
    "unsupported",
};

const char *hidpp_strerror(int code)
{
    if (code < 0 || (size_t)code >= sizeof(hidpp_errors) / sizeof(hidpp_errors[0]))
        return "?";
    return hidpp_errors[code];
}

int hidpp_build_request(uint8_t *out, size_t outlen, uint8_t devnum, uint8_t idx,
                        uint8_t fn, const uint8_t *params, size_t nparams)
{
    if (outlen < HIDPP_LONG_LEN || nparams > HIDPP_LONG_LEN - 4)
        return -1;
    out[0] = 0x11;
    out[1] = devnum;
    out[2] = idx;
    out[3] = (uint8_t)((fn << 4) | HIDPP_SW_ID);
    if (nparams)
        memcpy(out + 4, params, nparams);
    memset(out + 4 + nparams, 0, HIDPP_LONG_LEN - 4 - nparams);
    return HIDPP_LONG_LEN;
}

enum hidpp_reply_kind hidpp_classify(const uint8_t *buf, size_t len,
                                     uint8_t *devnum_out, uint8_t *idx_out,
                                     uint8_t *fnsw_out, uint8_t *params_out)
{
    uint8_t devnum, idx, fnsw;

    if (buf == NULL || len < 7)
        return HIDPP_REPLY_IGNORE;
    if (buf[0] != 0x10 && buf[0] != 0x11)
        return HIDPP_REPLY_IGNORE;
    devnum = buf[1];
    idx = buf[2];
    fnsw = buf[3];

    if (idx == 0x8F) {
        /* HID++ 1.0 error: [rep, dev, 0x8F, idx, fnsw, code, ...] */
        if (devnum_out)
            *devnum_out = devnum;
        if (idx_out)
            *idx_out = buf[3];
        if (fnsw_out)
            *fnsw_out = fnsw;
        if (params_out) {
            memset(params_out, 0, HIDPP_MAX_PARAMS);
            params_out[0] = buf[5];
        }
        return HIDPP_REPLY_ERR1;
    }
    if (idx == 0xFF) {
        /* HID++ 2.0 error: [rep, dev, 0xFF, featidx, fnsw, code, ...] */
        if (devnum_out)
            *devnum_out = devnum;
        if (idx_out)
            *idx_out = buf[3];
        if (fnsw_out)
            *fnsw_out = buf[4];
        if (params_out) {
            memset(params_out, 0, HIDPP_MAX_PARAMS);
            params_out[0] = buf[5];
        }
        return HIDPP_REPLY_ERR2;
    }
    /* data reply: params are bytes 4..19, zero padded past report length */
    if (devnum_out)
        *devnum_out = devnum;
    if (idx_out)
        *idx_out = idx;
    if (fnsw_out)
        *fnsw_out = fnsw;
    if (params_out) {
        memset(params_out, 0, HIDPP_MAX_PARAMS);
        memcpy(params_out, buf + 4, len - 4 > (size_t)HIDPP_MAX_PARAMS
                   ? (size_t)HIDPP_MAX_PARAMS : len - 4);
    }
    return HIDPP_REPLY_DATA;
}

int hidpp_scan(struct hidpp_candidate *out, int max)
{
    (void)out; (void)max;
    return 0;
}

int hidpp_open(struct hidpp_device *d, const struct hidpp_candidate *c)
{
    (void)d; (void)c;
    return RSLAP_RC_MALFORMED;
}

void hidpp_close(struct hidpp_device *d)
{
    (void)d;
}

int hidpp_request_idx(struct hidpp_device *d, uint8_t idx, uint8_t fn,
                      const uint8_t *params, size_t n, uint8_t out[HIDPP_MAX_PARAMS])
{
    (void)d; (void)idx; (void)fn; (void)params; (void)n; (void)out;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_ping(struct hidpp_device *d, uint8_t *proto_major, uint8_t *proto_minor)
{
    (void)d; (void)proto_major; (void)proto_minor;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_feature_index(struct hidpp_device *d, int feat_slot, uint16_t feature)
{
    (void)d; (void)feat_slot; (void)feature;
    return RSLAP_RC_UNSUPPORTED;
}

int hidpp_request(struct hidpp_device *d, int feat_slot, uint8_t fn,
                  const uint8_t *params, size_t n, uint8_t out[HIDPP_MAX_PARAMS])
{
    (void)d; (void)feat_slot; (void)fn; (void)params; (void)n; (void)out;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_name(struct hidpp_device *d, char *out, size_t outlen)
{
    (void)d; (void)out; (void)outlen;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_unit(struct hidpp_device *d, uint8_t unit_out[4], uint16_t models_out[3])
{
    (void)d; (void)unit_out; (void)models_out;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_firmware(struct hidpp_device *d, char *out, size_t outlen)
{
    (void)d; (void)out; (void)outlen;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_battery(struct hidpp_device *d, int *pct, const char **status)
{
    (void)d; (void)pct; (void)status;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_dpi_list(struct hidpp_device *d, int *vals, int max)
{
    (void)d; (void)vals; (void)max;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_live_dpi(struct hidpp_device *d, int *x, int *y, uint8_t *lod)
{
    (void)d; (void)x; (void)y; (void)lod;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_set_live_dpi(struct hidpp_device *d, int x, int y, uint8_t lod)
{
    (void)d; (void)x; (void)y; (void)lod;
    return RSLAP_RC_TIMEOUT;
}

int hidpp_get_live_rate(struct hidpp_device *d, int *hz)
{
    (void)d; (void)hz;
    return RSLAP_RC_TIMEOUT;
}
