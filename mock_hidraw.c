/* mock_hidraw.c — LD_PRELOAD fake for headless integration tests.
 *
 * Interposes open/open64/read/write/select/close/ioctl. Any open() under
 * $RSLAP_DEV_PREFIX is hijacked: the node named by $MOCK_MOUSE_NODE
 * (default hidraw0) becomes a minimal HID++ 2.0 G305 X (profile format 7,
 * golden sectors from golden.h, wpid 046d:40bb — the owner-confirmed
 * value); every other node behaves like a receiver slot with nothing
 * paired (HID++ 1.0 error replies).
 *
 * Replies are computed synchronously in write() and handed back by read();
 * select() reports readability exactly when a reply is pending, so the
 * program under test exercises its real request/reply loop.
 *
 * Build: cc -std=gnu99 -shared -fPIC -o mock_hidraw.so mock_hidraw.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <linux/hidraw.h>

#include "golden.h"

#define MOCK_MAX_FD 64
#define MOCK_REPLY_LEN 20
#define MOCK_NSECTORS 0x102

#define F_ROOT 0x0000
#define F_FW_VERSION 0x0003
#define F_DEVICE_NAME 0x0005
#define F_BATTERY_STATUS 0x1000
#define F_EXTENDED_DPI 0x2202
#define F_EXT_REPORT_RATE 0x8061
#define F_ONBOARD_PROFILES 0x8100

#define MOCK_WPID 0x40BB /* owner-confirmed G305 X LIGHTSPEED wireless wpid */

static const char mock_name[] = "Logitech G305 X";

struct mock_fd {
    int in_use;
    int dead; /* receiver slot: replies with HID++ 1.0 errors */
    uint8_t pending[MOCK_REPLY_LEN];
    size_t pending_len;
};

static struct mock_fd fds[MOCK_MAX_FD];

static uint8_t sectors[MOCK_NSECTORS][GOLDEN_SECTOR_SIZE];
static int sectors_loaded;
static int mode = 1; /* on-board memory mode */
static int cur_sector = 1;
static int write_sector = -1;
static int write_off;
static int live_dpi = 800;

/* Exactly 10 bytes, deliberately NOT NUL-terminated: the whole array is
 * written into the state file and memcmp'd back byte for byte. The
 * nonstring attribute tells GCC 15+ (-Wunterminated-string-initialization,
 * on by default there) that the truncated initializer is intentional. */
static const char MOCK_MAGIC[10] __attribute__((nonstring)) = "RSLAPMOCK1";

/* Sector state persists across processes via $MOCK_STATE so integration
 * steps that run the binary as separate invocations still see writes. */
static void save_state(void)
{
    const char *p = getenv("MOCK_STATE");
    FILE *f;
    int32_t v;

    if (!p || !p[0])
        return;
    f = fopen(p, "wb");
    if (!f)
        return;
    fwrite(MOCK_MAGIC, 1, sizeof(MOCK_MAGIC), f);
    v = mode;
    fwrite(&v, sizeof(v), 1, f);
    v = cur_sector;
    fwrite(&v, sizeof(v), 1, f);
    v = live_dpi;
    fwrite(&v, sizeof(v), 1, f);
    fwrite(sectors, 1, sizeof(sectors), f);
    fclose(f);
}

static void load_sectors(void)
{
    const char *p;

    if (sectors_loaded)
        return;
    sectors_loaded = 1;
    memset(sectors, 0xFF, sizeof(sectors));
    memcpy(sectors[0x101], GOLDEN_V7_ROM, GOLDEN_SECTOR_SIZE);
    memcpy(sectors[0x100], GOLDEN_ROMDIR, GOLDEN_SECTOR_SIZE);
    memcpy(sectors[0], GOLDEN_DIR, GOLDEN_SECTOR_SIZE);
    if (getenv("MOCK_BLANK_DIR")) {
        /* Factory-fresh device: blank user directory, ROM profile active —
         * the state a new G305 X ships in (owner-confirmed: every slot
         * shows "unused" and the active sector is the ROM one). */
        memset(sectors[0], 0xFF, GOLDEN_SECTOR_SIZE);
        if (getenv("MOCK_ROM_ACTIVE"))
            cur_sector = 0x0101;
    }
    memcpy(sectors[1], GOLDEN_V7, GOLDEN_SECTOR_SIZE);

    p = getenv("MOCK_STATE");
    if (p && p[0]) {
        FILE *f = fopen(p, "rb");

        if (f) {
            char magic[10];
            int32_t v[3];

            if (fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
                memcmp(magic, MOCK_MAGIC, sizeof(magic)) == 0 &&
                fread(v, 4, 3, f) == 3 &&
                v[0] >= 1 && v[0] <= 2 && v[1] >= 0 && v[1] < MOCK_NSECTORS &&
                fread(sectors, 1, sizeof(sectors), f) == sizeof(sectors)) {
                mode = (int)v[0];
                cur_sector = (int)v[1];
                live_dpi = (int)v[2];
            }
            fclose(f);
        }
    }
}

static int (*real_open)(const char *, int, ...);
static int (*real_close)(int);
static ssize_t (*real_read)(int, void *, size_t);
static ssize_t (*real_write)(int, const void *, size_t);
static int (*real_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
static int (*real_ioctl)(int, unsigned long, ...);

static void resolve_reals(void)
{
    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");
    if (!real_close)
        real_close = dlsym(RTLD_NEXT, "close");
    if (!real_read)
        real_read = dlsym(RTLD_NEXT, "read");
    if (!real_write)
        real_write = dlsym(RTLD_NEXT, "write");
    if (!real_select)
        real_select = dlsym(RTLD_NEXT, "select");
    if (!real_ioctl)
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
}

static struct mock_fd *fd_entry(int fd)
{
    if (fd < 0 || fd >= MOCK_MAX_FD)
        return NULL;
    return fds[fd].in_use ? &fds[fd] : NULL;
}

/* ------------------------------------------------------------ device model */

static int feature_idx(uint16_t feature)
{
    switch (feature) {
    case F_FW_VERSION: return 1;
    case F_DEVICE_NAME: return 2;
    case F_BATTERY_STATUS: return 3;
    case F_EXTENDED_DPI: return 4;
    case F_EXT_REPORT_RATE: return 5;
    case F_ONBOARD_PROFILES: return 6;
    default: return 0;
    }
}

/* is `sector` an enabled user profile? (the mock's directory is sector 0) */
static int sector_enabled(int sector)
{
    const uint8_t *d = sectors[0];

    for (int off = 0; off + 3 < GOLDEN_SECTOR_SIZE; off += 4) {
        int sec = (d[off] << 8) | d[off + 1];

        if (sec == 0xFFFF || sec == 0x0000)
            break;
        if (sec == sector)
            return d[off + 2] == 1;
    }
    return 0;
}

static void reply_data(uint8_t *reply, uint8_t devnum, uint8_t idx, uint8_t fnsw,
                       const uint8_t params[16])
{
    memset(reply, 0, MOCK_REPLY_LEN);
    reply[0] = 0x11;
    reply[1] = devnum;
    reply[2] = idx;
    reply[3] = fnsw;
    if (params)
        memcpy(reply + 4, params, 16);
}

static void reply_err2(uint8_t *reply, uint8_t devnum, uint8_t idx, uint8_t fnsw,
                       uint8_t code)
{
    memset(reply, 0, MOCK_REPLY_LEN);
    reply[0] = 0x11;
    reply[1] = devnum;
    reply[2] = 0xFF;
    reply[3] = idx;
    reply[4] = fnsw;
    reply[5] = code;
}

static void reply_err1(uint8_t *reply, uint8_t devnum, uint8_t idx, uint8_t fnsw)
{
    memset(reply, 0, MOCK_REPLY_LEN);
    reply[0] = 0x11;
    reply[1] = devnum;
    reply[2] = 0x8F;
    reply[3] = idx;
    reply[4] = fnsw;
    reply[5] = 0x01; /* "unknown": nothing paired on this slot */
}

/* serve one 0x8100 onboard-profiles request */
static void onboard_fn(uint8_t *reply, uint8_t devnum,
                       uint8_t idx, uint8_t fnsw, const uint8_t *p)
{
    int sector, off;

    switch (fnsw >> 4) {
    case 0: { /* info */
        uint8_t params[16] = {0};

        params[0] = 1;  /* memory model: profiles */
        params[1] = 7;  /* profile format 7 */
        params[2] = 1;  /* macro format */
        params[3] = 5;  /* profile count */
        params[4] = 5;  /* out-of-box profile count */
        params[5] = 6;  /* button count */
        params[6] = 12; /* sector count */
        params[7] = GOLDEN_SECTOR_SIZE >> 8;
        params[8] = GOLDEN_SECTOR_SIZE & 0xFF;
        params[9] = 0x02; /* gshift capable */
        reply_data(reply, devnum, idx, fnsw, params);
        return;
    }
    case 1: /* set mode */
        mode = p[0];
        save_state();
        reply_data(reply, devnum, idx, fnsw, NULL);
        return;
    case 2: { /* get mode */
        uint8_t params[16] = {0};

        params[0] = (uint8_t)mode;
        reply_data(reply, devnum, idx, fnsw, params);
        return;
    }
    case 3: /* set current sector */
        sector = (p[0] << 8) | p[1];
        if (!sector_enabled(sector)) {
            reply_err2(reply, devnum, idx, fnsw, 2); /* refused, like G305 X */
            return;
        }
        cur_sector = sector;
        save_state();
        reply_data(reply, devnum, idx, fnsw, NULL);
        return;
    case 4: { /* get current sector */
        uint8_t params[16] = {0};

        params[0] = (uint8_t)(cur_sector >> 8);
        params[1] = (uint8_t)cur_sector;
        reply_data(reply, devnum, idx, fnsw, params);
        return;
    }
    case 5: { /* read sector: 16 bytes at (sector, offset) */
        sector = (p[0] << 8) | p[1];
        off = (p[2] << 8) | p[3];
        if (sector < 0 || sector >= MOCK_NSECTORS || off < 0 ||
            off + 16 > GOLDEN_SECTOR_SIZE) {
            reply_err2(reply, devnum, idx, fnsw, 2);
            return;
        }
        reply_data(reply, devnum, idx, fnsw, sectors[sector] + off);
        return;
    }
    case 6: /* write start */
        sector = (p[0] << 8) | p[1];
        {
            int size = (p[4] << 8) | p[5];

            if (sector < 0 || sector >= 0x100 || sector >= MOCK_NSECTORS ||
                size != GOLDEN_SECTOR_SIZE) {
                reply_err2(reply, devnum, idx, fnsw, 2);
                return;
            }
        }
        write_sector = sector;
        write_off = 0;
        reply_data(reply, devnum, idx, fnsw, NULL);
        return;
    case 7: /* write data: 16 bytes */
        if (write_sector < 0 || write_off + 16 > GOLDEN_SECTOR_SIZE) {
            reply_err2(reply, devnum, idx, fnsw, 2);
            return;
        }
        memcpy(sectors[write_sector] + write_off, p, 16);
        write_off += 16;
        reply_data(reply, devnum, idx, fnsw, NULL);
        return;
    case 8: /* write end (data already landed, like real firmware) */
        save_state();
        reply_data(reply, devnum, idx, fnsw, NULL);
        return;
    case 0x0B: { /* get current dpi index */
        uint8_t params[16] = {0};

        params[0] = 1;
        reply_data(reply, devnum, idx, fnsw, params);
        return;
    }
    case 0x0C: /* set current dpi index */
        reply_data(reply, devnum, idx, fnsw, NULL);
        return;
    default:
        reply_err2(reply, devnum, idx, fnsw, 7); /* invalid function */
        return;
    }
}

static void handle_frame(const uint8_t *req, size_t len, uint8_t *reply)
{
    uint8_t devnum, idx, fnsw, fn;
    const uint8_t *p;

    (void)len;
    devnum = req[1];
    idx = req[2];
    fnsw = req[3];
    fn = (uint8_t)(fnsw >> 4);
    p = req + 4;

    if (req[0] != 0x11)
        return; /* not a long frame: ignore */

    if (idx == 0 && fn == 1) { /* ping */
        uint8_t params[16] = {0};

        params[0] = 2; /* HID++ 2.0 */
        params[1] = 0;
        params[2] = 0x5A;
        reply_data(reply, devnum, idx, fnsw, params);
        return;
    }
    if (idx == 0 && fn == 0) { /* root: feature index lookup */
        uint8_t params[16] = {0};
        uint16_t feature = (uint16_t)((p[0] << 8) | p[1]);

        params[0] = (uint8_t)feature_idx(feature);
        reply_data(reply, devnum, idx, fnsw, params);
        return;
    }

    switch (idx) {
    case 1: { /* 0x0003 firmware */
        if (fn == 0) {
            uint8_t params[16] = {0};

            params[0] = 1; /* one firmware entry */
            params[1] = 0xAA; /* unit id */
            params[2] = 0xBB;
            params[3] = 0xCC;
            params[4] = 0xDD;
            params[7] = MOCK_WPID >> 8; /* model id on transport 1 */
            params[8] = MOCK_WPID & 0xFF;
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        if (fn == 1) {
            uint8_t params[16] = {0};

            params[0] = 0x00; /* main application */
            params[1] = 'M';
            params[2] = 'P';
            params[3] = 'M';
            params[4] = 0x09;
            params[5] = 0x02;
            params[6] = 0x00;
            params[7] = 0x32;
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        reply_err2(reply, devnum, idx, fnsw, 7);
        return;
    }
    case 2: { /* 0x0005 device name */
        if (fn == 0) {
            uint8_t params[16] = {0};

            params[0] = (uint8_t)(sizeof(mock_name) - 1);
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        if (fn == 1) {
            uint8_t params[16] = {0};
            size_t off = p[0];

            if (off < sizeof(mock_name) - 1)
                memcpy(params, mock_name + off, sizeof(mock_name) - 1 - off);
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        reply_err2(reply, devnum, idx, fnsw, 7);
        return;
    }
    case 3: { /* 0x1000 battery status */
        if (fn == 0) {
            uint8_t params[16] = {0};

            params[0] = 85; /* percent */
            params[2] = 0;  /* Discharging */
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        reply_err2(reply, devnum, idx, fnsw, 7);
        return;
    }
    case 4: { /* 0x2202 extended adjustable DPI */
        if (fn == 2) { /* dpi list, one page is enough */
            uint8_t params[16] = {0};

            /* 400 800 1600 3200 6400 (u16 BE) then 00 00 terminator */
            params[3] = 0x01; params[4] = 0x90;
            params[5] = 0x03; params[6] = 0x20;
            params[7] = 0x06; params[8] = 0x40;
            params[9] = 0x0C; params[10] = 0x80;
            params[11] = 0x19; params[12] = 0x00;
            params[13] = 0x00; params[14] = 0x00;
            params[15] = 0x00;
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        if (fn == 5) { /* get dpi */
            uint8_t params[16] = {0};

            params[0] = 0;
            params[1] = (uint8_t)(live_dpi >> 8);
            params[2] = (uint8_t)live_dpi;
            params[5] = (uint8_t)(live_dpi >> 8);
            params[6] = (uint8_t)live_dpi;
            params[9] = 1; /* LOD */
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        if (fn == 6) { /* set dpi: refused in on-board mode (error 0x05) */
            if (mode == 1) {
                reply_err2(reply, devnum, idx, fnsw, 5);
                return;
            }
            live_dpi = (p[1] << 8) | p[2];
            save_state();
            reply_data(reply, devnum, idx, fnsw, NULL);
            return;
        }
        reply_err2(reply, devnum, idx, fnsw, 7);
        return;
    }
    case 5: { /* 0x8061 extended report rate */
        if (fn == 0) {
            uint8_t params[16] = {0};

            params[0] = 0; /* sensor 0 */
            params[1] = 3; /* 1000 Hz */
            reply_data(reply, devnum, idx, fnsw, params);
            return;
        }
        reply_err2(reply, devnum, idx, fnsw, 7);
        return;
    }
    case 6: /* 0x8100 on-board profiles */
        onboard_fn(reply, devnum, idx, fnsw, p);
        return;
    default:
        reply_err2(reply, devnum, idx, fnsw, 6); /* invalid feature index */
        return;
    }
}

/* ------------------------------------------------------------- interposers */

static int dev_prefix_len(void)
{
    const char *p = getenv("RSLAP_DEV_PREFIX");

    return (p && p[0]) ? (int)strlen(p) : 0;
}

static int is_mouse_node(const char *path)
{
    const char *mouse = getenv("MOCK_MOUSE_NODE");
    const char *base;

    if (!mouse || !mouse[0])
        mouse = "hidraw0";
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, mouse) == 0;
}

int open(const char *path, int flags, ...)
{
    int plen = dev_prefix_len();

    resolve_reals();
    if (plen > 0 && strncmp(path, getenv("RSLAP_DEV_PREFIX"), (size_t)plen) == 0 &&
        path[plen] == '/') {
        int fd = real_open("/dev/null", flags);

        if (fd >= 0 && fd < MOCK_MAX_FD) {
            load_sectors();
            memset(&fds[fd], 0, sizeof(fds[fd]));
            fds[fd].in_use = 1;
            fds[fd].dead = !is_mouse_node(path);
        }
        return fd;
    }
    return real_open(path, flags);
}

int open64(const char *path, int flags, ...)
{
    return open(path, flags);
}

int close(int fd)
{
    resolve_reals();
    if (fd >= 0 && fd < MOCK_MAX_FD && fds[fd].in_use)
        memset(&fds[fd], 0, sizeof(fds[fd]));
    return real_close(fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
    struct mock_fd *e;

    resolve_reals();
    e = fd_entry(fd);
    if (!e)
        return real_read(fd, buf, count);
    if (e->pending_len == 0) {
        errno = EAGAIN;
        return -1;
    }
    {
        size_t n = e->pending_len < count ? e->pending_len : count;

        memcpy(buf, e->pending, n);
        e->pending_len = 0;
        return (ssize_t)n;
    }
}

ssize_t write(int fd, const void *buf, size_t count)
{
    struct mock_fd *e;
    uint8_t reply[MOCK_REPLY_LEN];

    resolve_reals();
    e = fd_entry(fd);
    if (!e)
        return real_write(fd, buf, count);
    if (count < 7 || ((const uint8_t *)buf)[0] != 0x11)
        return (ssize_t)count; /* not a long HID++ frame: swallow */

    if (e->dead) {
        reply_err1(reply, ((const uint8_t *)buf)[1],
                   ((const uint8_t *)buf)[2], ((const uint8_t *)buf)[3]);
    } else {
        handle_frame((const uint8_t *)buf, count, reply);
    }
    memcpy(e->pending, reply, MOCK_REPLY_LEN);
    e->pending_len = MOCK_REPLY_LEN;
    return (ssize_t)count;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    int n_pending = 0, n_mock = 0, n_other = 0;

    resolve_reals();
    if (readfds) {
        for (int fd = 0; fd < nfds && fd < MOCK_MAX_FD; fd++) {
            if (!FD_ISSET(fd, readfds))
                continue;
            if (fds[fd].in_use) {
                n_mock++;
                if (fds[fd].pending_len > 0)
                    n_pending++;
            } else {
                n_other++;
            }
        }
    }
    if (n_mock > 0 && n_other == 0) {
        if (n_pending > 0) {
            FD_ZERO(readfds);
            for (int fd = 0; fd < nfds && fd < MOCK_MAX_FD; fd++)
                if (fds[fd].in_use && fds[fd].pending_len > 0)
                    FD_SET(fd, readfds);
            return n_pending;
        }
        /* nothing pending: emulate a device that stays silent */
        if (timeout) {
            long us = timeout->tv_sec * 1000000L + timeout->tv_usec;

            if (us > 5000000)
                us = 5000000;
            if (us > 0) {
                struct timespec ts = {us / 1000000, (us % 1000000) * 1000};

                nanosleep(&ts, NULL);
            }
        }
        FD_ZERO(readfds);
        return 0;
    }
    return real_select(nfds, readfds, writefds, exceptfds, timeout);
}

int ioctl(int fd, unsigned long request, ...)
{
    struct mock_fd *e;
    void *arg;

    resolve_reals();
    e = fd_entry(fd);
    if (!e)
        return real_ioctl(fd, request, 0);

    /* fetch the vararg pointer for the calls we care about */
    {
        va_list ap;
        void *p;

        va_start(ap, request);
        p = va_arg(ap, void *);
        va_end(ap);
        arg = p;
    }

    if (request == (unsigned long)HIDIOCGRAWINFO && arg) {
        struct hidraw_devinfo *info = arg;

        info->bustype = 0x03;
        info->vendor = 0x046D;
        info->product = MOCK_WPID;
        return 0;
    }
    errno = ENOTTY;
    return -1;
}
