/* stubs for the device-facing transport; frame helpers are real
 * (test_hidpp.c drove their implementation). */
#include "hidpp.h"
#include "onboard.h" /* report-rate table for live-rate display */
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
        /* HID++ 1.0 error: [rep, dev, 0x8F, feature idx, fnsw, code, ...] */
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

/* ------------------------------------------------------------ sysfs scan */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <linux/hidraw.h>

/* small whole-file read for sysfs attribute files */
static int read_small_file(const char *path, char *out, size_t outlen)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0)
        return -1;
    n = read(fd, out, outlen - 1);
    close(fd);
    if (n < 0)
        return -1;
    out[n] = '\0';
    return 0;
}

/* HID++ capable interfaces declare vendor report ID 0x11 with a vendor
 * usage page (mirror of OpenGMice's descriptor check). */
static int has_hidpp_reports(const char *node, const char *sysfs)
{
    char path[512];
    uint8_t desc[4096];
    char buf[4096];
    ssize_t n;
    int fd;

    snprintf(path, sizeof(path), "%.200s/%.50s/device/report_descriptor", sysfs, node);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    n = read(fd, desc, sizeof(desc));
    close(fd);
    if (n < 2)
        return 0;
    for (ssize_t i = 0; i + 1 < n; i++) {
        if (desc[i] == 0x85 && desc[i + 1] == 0x11) {
            for (ssize_t j = 0; j + 2 < n; j++) {
                if (desc[j] == 0x06 &&
                    ((desc[j + 1] == 0x00 && desc[j + 2] == 0xFF) ||
                     (desc[j + 1] == 0x43 && desc[j + 2] == 0xFF)))
                    return 1;
            }
            return 0;
        }
    }
    (void)buf;
    return 0;
}

static int is_receiver(int bus, uint16_t pid)
{
    /* Unifying, LIGHTSPEED and Bolt receivers all live in 046D:C5xx */
    return bus == 0x03 && pid >= 0xC500 && pid <= 0xC5FF;
}

int hidpp_scan(struct hidpp_candidate *out, int max)
{
    const char *sysfs = getenv("RSLAP_SYSFS_PATH");
    const char *devpfx = getenv("RSLAP_DEV_PREFIX");
    DIR *dir;
    struct dirent *de;
    int n = 0, nrecv = 0;
    struct hidpp_candidate recv_out[8];

    if (!sysfs)
        sysfs = "/sys/class/hidraw";
    if (!devpfx)
        devpfx = "/dev";

    dir = opendir(sysfs);
    if (dir == NULL)
        return 0;

    while ((de = readdir(dir)) != NULL) {
        char path[512], buf[2048];
        struct hidpp_candidate c;
        char *hid_id = NULL, *hid_name = NULL, *driver = NULL;

        if (de->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%.200s/%.50s/device/uevent", sysfs, de->d_name);
        if (read_small_file(path, buf, sizeof(buf)) < 0)
            continue;

        /* parse the uevent key=value lines we care about */
        for (char *line = buf; *line; ) {
            char *nl = strchr(line, '\n');
            char *eq = strchr(line, '=');

            if (nl)
                *nl = '\0';
            if (eq) {
                *eq = '\0';
                if (strcmp(line, "HID_ID") == 0)
                    hid_id = eq + 1;
                else if (strcmp(line, "HID_NAME") == 0)
                    hid_name = eq + 1;
                else if (strcmp(line, "DRIVER") == 0)
                    driver = eq + 1;
            }
            if (!nl)
                break;
            line = nl + 1;
        }

        memset(&c, 0, sizeof(c));
        if (hid_id) {
            unsigned bus = 0, vid = 0, pid = 0;

            if (sscanf(hid_id, "%x:%x:%x", &bus, &vid, &pid) == 3) {
                c.bus = (int)bus;
                c.vid = (uint16_t)vid;
                c.pid = (uint16_t)pid;
            }
        }
        /* keep HID++ endpoints only: the G300s is discovered through
         * its usbfs presence instead (see g300s_scan) */
        if (c.vid != RSLAP_VID_LOGITECH || !has_hidpp_reports(de->d_name, sysfs))
            continue;

        snprintf(c.path, sizeof(c.path), "%.200s/%.40s", devpfx, de->d_name);
        if (hid_name)
            snprintf(c.name, sizeof(c.name), "%.127s", hid_name);

        if (is_receiver(c.bus, c.pid) && nrecv < (int)(sizeof(recv_out) / sizeof(recv_out[0]))) {
            /* receiver node: one entry per paired-device slot */
            for (int slot = 1; slot <= 6 && nrecv < (int)(sizeof(recv_out) / sizeof(recv_out[0])); slot++) {
                struct hidpp_candidate r = c;

                snprintf(r.name, sizeof(r.name), "%.100s slot %d",
                         c.name[0] ? c.name : "receiver", slot);
                snprintf(r.link, sizeof(r.link), "receiver");
                r.devnum = slot;
                r.strict = 1;
                recv_out[nrecv++] = r;
            }
        } else {
            /* direct node, or hid-logitech-dj child for a paired device */
            c.devnum = 0xFF;
            c.strict = 0;
            if (driver && strcmp(driver, "logitech-hidpp-device") == 0)
                snprintf(c.link, sizeof(c.link), "wireless");
            else if (c.bus == 0x05)
                snprintf(c.link, sizeof(c.link), "Bluetooth");
            else
                snprintf(c.link, sizeof(c.link), "USB");
            if (n < max)
                out[n++] = c;
        }
    }
    closedir(dir);

    /* receiver slots last: dj child nodes answer first when present */
    for (int i = 0; i < nrecv && n < max; i++)
        out[n++] = recv_out[i];
    return n;
}

/* ------------------------------------------------------------- transport */

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int wait_readable(int fd, int ms)
{
    fd_set rfds;
    struct timeval tv;

    if (ms < 0)
        ms = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(fd + 1, &rfds, NULL, NULL, &tv);
}

int hidpp_open(struct hidpp_device *d, const struct hidpp_candidate *c)
{
    struct hidraw_devinfo info;

    memset(d, 0, sizeof(*d));
    d->fd = open(c->path, O_RDWR | O_NONBLOCK);
    if (d->fd < 0)
        return RSLAP_RC_MALFORMED;
    d->devnum = (uint8_t)c->devnum;
    d->strict = c->strict;
    d->timeout_ms = 2000;
    d->bus = c->bus;
    d->vid = c->vid;
    d->pid = c->pid;

    /* authoritative VID:PID straight from the kernel */
    if (ioctl(d->fd, HIDIOCGRAWINFO, &info) == 0) {
        d->vid = (uint16_t)info.vendor;
        d->pid = (uint16_t)info.product;
        d->bus = (int)info.bustype;
    }
    return RSLAP_RC_OK;
}

void hidpp_close(struct hidpp_device *d)
{
    if (d->fd >= 0)
        close(d->fd);
    d->fd = -1;
}

int hidpp_request_idx(struct hidpp_device *d, uint8_t idx, uint8_t fn,
                      const uint8_t *params, size_t n, uint8_t out[HIDPP_MAX_PARAMS])
{
    uint8_t req[HIDPP_LONG_LEN];
    uint8_t rep[64];
    uint8_t rdev, ridx, rfnsw, rparams[HIDPP_MAX_PARAMS];
    int reqlen, len;
    enum hidpp_reply_kind kind;
    long deadline;

    reqlen = hidpp_build_request(req, sizeof(req), d->devnum, idx, fn, params, n);
    if (reqlen < 0)
        return RSLAP_RC_MALFORMED;

    /* drain stale input reports first */
    for (;;) {
        len = (int)read(d->fd, rep, sizeof(rep));
        if (len <= 0)
            break;
    }
    if (write(d->fd, req, (size_t)reqlen) != reqlen)
        return RSLAP_RC_MALFORMED;

    deadline = now_ms() + d->timeout_ms;
    for (;;) {
        long left = deadline - now_ms();

        if (left <= 0)
            return RSLAP_RC_TIMEOUT;
        len = wait_readable(d->fd, (int)left);
        if (len < 0)
            return RSLAP_RC_MALFORMED;
        if (len == 0)
            return RSLAP_RC_TIMEOUT;
        len = (int)read(d->fd, rep, sizeof(rep));
        if (len < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return RSLAP_RC_MALFORMED;
        }
        kind = hidpp_classify(rep, (size_t)len, &rdev, &ridx, &rfnsw, rparams);
        if (kind == HIDPP_REPLY_IGNORE)
            continue;
        if (d->strict && rdev != d->devnum)
            continue;
        if (kind == HIDPP_REPLY_ERR1 && ridx == idx && rfnsw == req[3]) {
            d->err.code = rparams[0];
            d->err.feature = idx;
            d->err.fn = fn;
            return RSLAP_RC_HIDPP;
        }
        if (kind == HIDPP_REPLY_ERR2 && ridx == idx && (rfnsw & 0x0F) == HIDPP_SW_ID) {
            d->err.code = rparams[0];
            d->err.feature = idx;
            d->err.fn = fn;
            return RSLAP_RC_HIDPP;
        }
        if (kind == HIDPP_REPLY_DATA && ridx == idx && rfnsw == req[3]) {
            if (out)
                memcpy(out, rparams, HIDPP_MAX_PARAMS);
            return RSLAP_RC_OK;
        }
        /* otherwise: a reply for someone else; keep waiting */
    }
}

int hidpp_ping(struct hidpp_device *d, uint8_t *proto_major, uint8_t *proto_minor)
{
    const uint8_t ping_params[3] = {0x00, 0x00, 0x5A};
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc = hidpp_request_idx(d, 0, 0x01, ping_params, 3, out);

    if (rc != RSLAP_RC_OK)
        return rc;
    if (proto_major)
        *proto_major = out[0];
    if (proto_minor)
        *proto_minor = out[1];
    return RSLAP_RC_OK;
}

static const uint16_t slot_features[FEAT__COUNT] = {
    F_FW_VERSION, F_DEVICE_NAME, F_BATTERY_STATUS, F_UNIFIED_BATTERY,
    F_ADJUSTABLE_DPI, F_EXTENDED_DPI, F_REPORT_RATE, F_EXT_REPORT_RATE,
    F_ONBOARD_PROFILES,
};

int hidpp_feature_index(struct hidpp_device *d, int feat_slot, uint16_t feature)
{
    uint8_t params[2] = {(uint8_t)(feature >> 8), (uint8_t)(feature & 0xFF)};
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc;

    if (feat_slot < 0 || feat_slot >= FEAT__COUNT)
        return RSLAP_RC_MALFORMED;
    if (d->have_idx[feat_slot])
        return d->idx[feat_slot] ? RSLAP_RC_OK : RSLAP_RC_UNSUPPORTED;

    rc = hidpp_request_idx(d, 0, 0x00, params, 2, out);
    if (rc != RSLAP_RC_OK)
        return rc;
    d->have_idx[feat_slot] = 1;
    d->idx[feat_slot] = out[0];
    if (out[0] == 0)
        return RSLAP_RC_UNSUPPORTED;
    return RSLAP_RC_OK;
}

int hidpp_request(struct hidpp_device *d, int feat_slot, uint8_t fn,
                  const uint8_t *params, size_t n, uint8_t out[HIDPP_MAX_PARAMS])
{
    int rc = hidpp_feature_index(d, feat_slot, slot_features[feat_slot]);

    if (rc != RSLAP_RC_OK) {
        d->err.feature = slot_features[feat_slot];
        d->err.fn = fn;
        return rc;
    }
    rc = hidpp_request_idx(d, d->idx[feat_slot], fn, params, n, out);
    if (rc == RSLAP_RC_HIDPP)
        d->err.feature = slot_features[feat_slot];
    return rc;
}

/* Send fn as a SHORT-report request (report ID 0x10, no software-id
 * nibble, up to 3 params). SetCurrentProfile, SetOnboardMode and
 * EndWrite are only defined on the short report: G HUB and libratbag
 * send them that way, and the G305 X firmware rejects the long form
 * of SetCurrentProfile and EndWrite with INVALID_ARGUMENT
 * (owner-confirmed on real hardware). */
int hidpp_request_short(struct hidpp_device *d, int feat_slot, uint8_t fn,
                        const uint8_t *params, size_t n)
{
    uint8_t req[HIDPP_LONG_LEN];
    uint8_t rep[64];
    uint8_t rdev, ridx, rfnsw, rparams[HIDPP_MAX_PARAMS];
    uint8_t idx, fnsw = (uint8_t)(fn << 4);
    enum hidpp_reply_kind kind;
    long deadline;
    int rc, len;

    if (n > 3)
        return RSLAP_RC_MALFORMED;
    rc = hidpp_feature_index(d, feat_slot, slot_features[feat_slot]);
    if (rc != RSLAP_RC_OK) {
        d->err.feature = slot_features[feat_slot];
        d->err.fn = fn;
        return rc;
    }
    idx = d->idx[feat_slot];

    memset(req, 0, sizeof(req));
    req[0] = 0x10;
    req[1] = d->devnum;
    req[2] = idx;
    req[3] = fnsw;
    if (n > 0)
        req[4] = params[0];
    if (n > 1)
        req[5] = params[1];
    if (n > 2)
        req[6] = params[2];

    /* drain stale input reports first */
    for (;;) {
        len = (int)read(d->fd, rep, sizeof(rep));
        if (len <= 0)
            break;
    }
    if (write(d->fd, req, 7) != 7)
        return RSLAP_RC_MALFORMED;

    deadline = now_ms() + d->timeout_ms;
    for (;;) {
        long left = deadline - now_ms();

        if (left <= 0)
            return RSLAP_RC_TIMEOUT;
        len = wait_readable(d->fd, (int)left);
        if (len < 0)
            return RSLAP_RC_MALFORMED;
        if (len == 0)
            return RSLAP_RC_TIMEOUT;
        len = (int)read(d->fd, rep, sizeof(rep));
        if (len < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return RSLAP_RC_MALFORMED;
        }
        kind = hidpp_classify(rep, (size_t)len, &rdev, &ridx, &rfnsw, rparams);
        if (kind == HIDPP_REPLY_IGNORE)
            continue;
        if (d->strict && rdev != d->devnum)
            continue;
        if (kind == HIDPP_REPLY_ERR2 && ridx == idx &&
            (rfnsw == fnsw || (rfnsw & 0x0F) == HIDPP_SW_ID)) {
            d->err.code = rparams[0];
            d->err.feature = slot_features[feat_slot];
            d->err.fn = fn;
            return RSLAP_RC_HIDPP;
        }
        if (kind == HIDPP_REPLY_DATA && ridx == idx && rfnsw == fnsw)
            return RSLAP_RC_OK;
    }
}

/* ---------------------------------------------------- feature conveniences */

int hidpp_get_name(struct hidpp_device *d, char *out, size_t outlen)
{
    uint8_t outp[HIDPP_MAX_PARAMS];
    int rc, len, off = 0;

    rc = hidpp_request(d, FEAT_NAME, 0, NULL, 0, outp);
    if (rc != RSLAP_RC_OK)
        return rc;
    len = outp[0];
    if (len <= 0 || (size_t)len >= outlen)
        len = (int)outlen - 1;
    while (off < len) {
        uint8_t params[1] = {(uint8_t)off};

        rc = hidpp_request(d, FEAT_NAME, 1, params, 1, outp);
        if (rc != RSLAP_RC_OK)
            return rc;
        for (int i = 0; i < HIDPP_MAX_PARAMS && off < len; i++)
            out[off++] = (char)outp[i];
    }
    out[len] = '\0';
    return RSLAP_RC_OK;
}

int hidpp_get_unit(struct hidpp_device *d, uint8_t unit_out[4], uint16_t models_out[3])
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc = hidpp_request(d, FEAT_FW, 0, NULL, 0, out);

    if (rc != RSLAP_RC_OK)
        return rc;
    if (unit_out) {
        for (int i = 0; i < 4; i++)
            unit_out[i] = out[1 + i];
    }
    if (models_out) {
        models_out[0] = rslap_be16(out + 7);
        models_out[1] = rslap_be16(out + 9);
        models_out[2] = rslap_be16(out + 11);
    }
    return RSLAP_RC_OK;
}

int hidpp_get_firmware(struct hidpp_device *d, char *out, size_t outlen)
{
    uint8_t outp[HIDPP_MAX_PARAMS];
    int rc, count;

    rc = hidpp_request(d, FEAT_FW, 0, NULL, 0, outp);
    if (rc != RSLAP_RC_OK)
        return rc;
    count = outp[0];
    out[0] = '\0';
    for (int i = 0; i < count && i < 8; i++) {
        uint8_t params[1] = {(uint8_t)i};

        rc = hidpp_request(d, FEAT_FW, 1, params, 1, outp);
        if (rc != RSLAP_RC_OK)
            return rc;
        if ((outp[0] & 0x0F) == 0) { /* main application firmware */
            snprintf(out, outlen, "%.3s %02X.%02X.%04X",
                     (const char *)outp + 1, outp[4], outp[5], rslap_be16(outp + 6));
            return RSLAP_RC_OK;
        }
    }
    return RSLAP_RC_UNSUPPORTED;
}

int hidpp_get_battery(struct hidpp_device *d, int *pct, const char **status)
{
    static const char *const statuses[] = {"Discharging", "Charging", "Almost full",
                                           "Full", "Slow charging", "Invalid battery",
                                           "Thermal error"};
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc;

    rc = hidpp_request(d, FEAT_UNIFIED_BATTERY, 1, NULL, 0, out);
    if (rc == RSLAP_RC_OK) {
        int p = out[0];
        int level = out[1];
        int st = out[2];

        if (p == 0) {
            if (level & 0x08)
                p = 100;
            else if (level & 0x04)
                p = 60;
            else if (level & 0x02)
                p = 20;
            else if (level & 0x01)
                p = 5;
        }
        *pct = p;
        *status = (st >= 0 && (size_t)st < sizeof(statuses) / sizeof(statuses[0]))
                      ? statuses[st] : "?";
        return RSLAP_RC_OK;
    }
    rc = hidpp_request(d, FEAT_BATTERY, 0, NULL, 0, out);
    if (rc != RSLAP_RC_OK)
        return rc;
    *pct = out[0];
    {
        int st = out[2];

        *status = (st >= 0 && (size_t)st < sizeof(statuses) / sizeof(statuses[0]))
                      ? statuses[st] : "?";
    }
    return RSLAP_RC_OK;
}

/* decode the 0x2202/0x2201 DPI list: explicit u16 BE values, or
 * 0xE000|step followed by an upper bound */
static int dpi_list_from_data(const uint8_t *data, size_t len, int *vals, int max)
{
    int n = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
        int v = (int)rslap_be16(data + i);

        if (v == 0)
            break;
        if ((v >> 13) == 7 && n > 0) {
            int step = v & 0x1FFF;
            int last = (i + 3 < len) ? (int)rslap_be16(data + i + 2) : 0;

            for (int s = vals[n - 1] + step; s <= last && n < max; s += step)
                vals[n++] = s;
            i += 2;
            continue;
        }
        if (n < max)
            vals[n++] = v;
        else
            break;
    }
    return n;
}

int hidpp_get_dpi_list(struct hidpp_device *d, int *vals, int max)
{
    uint8_t data[32 * 16];
    size_t have = 0;
    int n = 0;

    if (hidpp_feature_index(d, FEAT_DPI_EXT, F_EXTENDED_DPI) == RSLAP_RC_OK) {
        for (int page = 0; page < 32 && have + 13 <= sizeof(data); page++) {
            uint8_t params[3] = {0, 0, (uint8_t)page};
            uint8_t out[HIDPP_MAX_PARAMS];

            if (hidpp_request(d, FEAT_DPI_EXT, 2, params, 3, out) != RSLAP_RC_OK)
                break;
            memcpy(data + have, out + 3, 13);
            have += 13;
            if (have >= 2 && data[have - 1] == 0 && data[have - 2] == 0)
                break;
        }
    } else if (hidpp_feature_index(d, FEAT_DPI, F_ADJUSTABLE_DPI) == RSLAP_RC_OK) {
        for (int page = 0; page < 32 && have + 15 <= sizeof(data); page++) {
            uint8_t params[2] = {0, (uint8_t)page};
            uint8_t out[HIDPP_MAX_PARAMS];

            if (hidpp_request(d, FEAT_DPI, 1, params, 2, out) != RSLAP_RC_OK)
                break;
            memcpy(data + have, out + 1, 15);
            have += 15;
            if (have >= 2 && data[have - 1] == 0 && data[have - 2] == 0)
                break;
        }
    } else {
        return RSLAP_RC_UNSUPPORTED;
    }
    n = dpi_list_from_data(data, have, vals, max);
    return n > 0 ? n : RSLAP_RC_UNSUPPORTED;
}

int hidpp_get_live_dpi(struct hidpp_device *d, int *x, int *y, uint8_t *lod)
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc;

    rc = hidpp_request(d, FEAT_DPI_EXT, 5, (const uint8_t[]){0}, 1, out);
    if (rc == RSLAP_RC_OK) {
        *x = rslap_be16(out + 1);
        *y = rslap_be16(out + 5);
        if (*y == 0)
            *y = *x;
        if (lod)
            *lod = out[9];
        return RSLAP_RC_OK;
    }
    rc = hidpp_request(d, FEAT_DPI, 2, (const uint8_t[]){0}, 1, out);
    if (rc != RSLAP_RC_OK)
        return rc;
    *x = rslap_be16(out + 1);
    *y = *x;
    if (lod)
        *lod = 0;
    return RSLAP_RC_OK;
}

int hidpp_set_live_dpi(struct hidpp_device *d, int x, int y, uint8_t lod)
{
    uint8_t params[6] = {0, (uint8_t)(x >> 8), (uint8_t)x,
                         (uint8_t)(y >> 8), (uint8_t)y, lod};

    return hidpp_request(d, FEAT_DPI_EXT, 6, params, 6, NULL);
}

int hidpp_get_live_rate(struct hidpp_device *d, int *hz)
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc;

    rc = hidpp_request(d, FEAT_RATE_EXT, 0, (const uint8_t[]){0}, 1, out);
    if (rc == RSLAP_RC_OK) {
        *hz = rslap_rate_from_index(out[1]);
        return RSLAP_RC_OK;
    }
    rc = hidpp_request(d, FEAT_RATE, 0, NULL, 0, out);
    if (rc != RSLAP_RC_OK)
        return rc;
    *hz = rslap_rate_from_index(out[0]);
    return RSLAP_RC_OK;
}
