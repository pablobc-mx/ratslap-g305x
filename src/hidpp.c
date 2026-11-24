/* stub — HID++ transport implemented after the tests exist. */
#include "hidpp.h"

const char *hidpp_strerror(int code)
{
    (void)code;
    return "stub";
}

int hidpp_build_request(uint8_t *out, size_t outlen, uint8_t devnum, uint8_t idx,
                        uint8_t fn, const uint8_t *params, size_t nparams)
{
    (void)out; (void)outlen; (void)devnum; (void)idx; (void)fn; (void)params; (void)nparams;
    return -1;
}

enum hidpp_reply_kind hidpp_classify(const uint8_t *buf, size_t len,
                                     uint8_t *devnum_out, uint8_t *idx_out,
                                     uint8_t *fnsw_out, uint8_t *params_out)
{
    (void)buf; (void)len; (void)devnum_out; (void)idx_out; (void)fnsw_out; (void)params_out;
    return HIDPP_REPLY_IGNORE;
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
