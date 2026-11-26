/* ratslap-g305x — Logitech G300/G300s transport and mode decode.
 *
 * The G300s is not an HID++ device. Its three profiles ("modes") are
 * stored in vendor feature reports 0xF3/0xF4/0xF5 on the second USB
 * interface (HID_PHYS ...input1), 35 bytes each including the report
 * id. Reads need no session handshake (owner-confirmed: identical
 * bytes with and without the 0xF0 editor sequence) — but they DO need
 * the interface free of the kernel HID stack: while usbhid polls it,
 * the firmware answers every mode GET with 35 zero bytes. The
 * transport therefore claims interface 1 via usbfs and speaks raw
 * control transfers (the reference tool's libusb carrier, minus the
 * 2013 detach ceremony, which the kernel now provides atomically via
 * USBDEVFS_DISCONNECT_CLAIM).
 *
 * Wire layout of a mode block, confirmed against krayon's annotated
 * dump "F5 04 03 02 8406 0844 01 00 00 ...":
 *   raw[0]     report id (0xF3..0xF5)
 *   raw[1]     LED colour index (8 colours)
 *   raw[2]     report rate index into {1000,125,250,500}
 *   raw[3..6]  DPI stages: bit 7 marks the default, low nibble is
 *              the DPI point (n*250, 0 -> 4000)
 *   raw[7]     DPI shift point; bit 0x40 disables the shift button
 *   raw[8..34] nine bindings, 3 bytes each: mouse-button nibble,
 *              modifier mask, keyboard usage id
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "util.h"
#include "g300s.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define G300S_RID_F3 0xf3

/* The device's own rate table; the index order is the wired truth. */
static const int rate_table[4] = { 1000, 125, 250, 500 };

static const char *const colour_table[8] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"
};

/* Indexed by the low nibble of a binding's first byte. Buttons 4 and 5
 * (wheel tilt) are not addressable; the table jumps to Button6. */
static const char *const button_table[16] = {
    "NONE",    "Button1",  "Button2",  "Button3",
    "Button6", "Button7",  "Button8",  "Button9",
    "Button10","Button11", "DPIUp",    "DPIDown",
    "DPICycle","ModeSwitch","DPIShift", "DPIDefault"
};

/* Modifier mask bit i corresponds to usage 0xE0 + i (HID order). */
static const char *const mod_table[8] = {
    "LeftCtrl", "LeftShift", "LeftAlt", "LeftGUI",
    "RightCtrl", "RightShift", "RightAlt", "RightGUI"
};

/* Keyboard usage ids used by bindings (USB HID usage tables). */
static const struct { uint8_t usage; const char name[12]; } key_table[] = {
    { 0x04, "A" }, { 0x05, "B" }, { 0x06, "C" }, { 0x07, "D" },
    { 0x08, "E" }, { 0x09, "F" }, { 0x0a, "G" }, { 0x0b, "H" },
    { 0x0c, "I" }, { 0x0d, "J" }, { 0x0e, "K" }, { 0x0f, "L" },
    { 0x10, "M" }, { 0x11, "N" }, { 0x12, "O" }, { 0x13, "P" },
    { 0x14, "Q" }, { 0x15, "R" }, { 0x16, "S" }, { 0x17, "T" },
    { 0x18, "U" }, { 0x19, "V" }, { 0x1a, "W" }, { 0x1b, "X" },
    { 0x1c, "Y" }, { 0x1d, "Z" },
    { 0x1e, "1" }, { 0x1f, "2" }, { 0x20, "3" }, { 0x21, "4" },
    { 0x22, "5" }, { 0x23, "6" }, { 0x24, "7" }, { 0x25, "8" },
    { 0x26, "9" }, { 0x27, "0" },
    { 0x28, "Enter" }, { 0x29, "Esc" }, { 0x2a, "Backspace" },
    { 0x2b, "Tab" }, { 0x2c, "Space" },
    { 0x2d, "-" }, { 0x2e, "=" }, { 0x2f, "[" }, { 0x30, "]" },
    { 0x31, "\\" }, { 0x32, ";" }, { 0x33, "'" }, { 0x34, "`" },
    { 0x35, "," }, { 0x36, "." }, { 0x37, "/" }, { 0x38, "CapsLock" },
    { 0x39, "F1" }, { 0x3a, "F2" }, { 0x3b, "F3" }, { 0x3c, "F4" },
    { 0x3d, "F5" }, { 0x3e, "F6" }, { 0x3f, "F7" }, { 0x40, "F8" },
    { 0x41, "F9" }, { 0x42, "F10" }, { 0x43, "F11" }, { 0x44, "F12" },
    { 0x45, "PrintScreen" }, { 0x46, "ScrollLock" }, { 0x47, "Pause" },
    { 0x48, "Insert" }, { 0x49, "Home" }, { 0x4a, "PageUp" },
    { 0x4b, "Delete" }, { 0x4c, "End" }, { 0x4d, "PageDown" },
    { 0x4e, "Right" }, { 0x4f, "Left" }, { 0x50, "Down" }, { 0x51, "Up" },
    { 0x52, "NumLock" }, { 0x53, "Num/" }, { 0x54, "Num*" },
    { 0x55, "Num-" }, { 0x56, "Num+" }, { 0x57, "NumEnter" },
    { 0x58, "Num1" }, { 0x59, "Num2" }, { 0x5a, "Num3" }, { 0x5b, "Num4" },
    { 0x5c, "Num5" }, { 0x5d, "Num6" }, { 0x5e, "Num7" }, { 0x5f, "Num8" },
    { 0x60, "Num9" }, { 0x61, "Num0" }, { 0x62, "Num." },
    { 0x65, "Application" },
};

/* DPI point -> DPI: the nibble is an index of 250-DPI steps with a
 * special case for 0 (the factory dump's 4000). */
static int dpi_point(int n)
{
    return n ? n * 250 : 4000;
}

/* Read one sysfs id file (idVendor, idProduct, busnum, devnum) into a
 * short string with trailing whitespace stripped. */
static int read_id(const char *base, const char *dev, const char *leaf,
                   char *out, size_t outlen)
{
    char path[512];
    ssize_t n;
    int fd;

    snprintf(path, sizeof(path), "%.200s/%.40s/%s", base, dev, leaf);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    n = read(fd, out, outlen - 1);
    close(fd);
    if (n <= 0)
        return 0;
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
        out[--n] = '\0';
    return 1;
}

int g300s_scan(char *node_out, size_t node_len, char *if_out, size_t if_len)
{
    const char *base = getenv("RSLAP_USB_SYSFS_PATH");
    struct dirent *de;
    DIR *dir;
    int found = 0;

    if (!base)
        base = "/sys/bus/usb/devices";
    dir = opendir(base);
    if (dir == NULL)
        return 0;
    while ((de = readdir(dir)) != NULL) {
        char vid[16], pid[16], bus[16], dev[16];

        if (de->d_name[0] == '.' || strchr(de->d_name, ':'))
            continue; /* interfaces carry a colon; devices do not */
        if (!read_id(base, de->d_name, "idVendor", vid, sizeof(vid)) ||
            strcmp(vid, "046d") != 0)
            continue;
        if (!read_id(base, de->d_name, "idProduct", pid, sizeof(pid)) ||
            strcmp(pid, "c246") != 0)
            continue;
        if (read_id(base, de->d_name, "busnum", bus, sizeof(bus)) &&
            read_id(base, de->d_name, "devnum", dev, sizeof(dev))) {
            snprintf(node_out, node_len, "/dev/bus/usb/%03d/%03d",
                     atoi(bus), atoi(dev));
            snprintf(if_out, if_len, "%.40s:1.1", de->d_name);
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

int g300s_open(struct g300s_usbdev *d, const char *node, const char *ifname)
{
    memset(d, 0, sizeof(*d));
    d->fd = open(node, O_RDWR);
    if (d->fd < 0)
        return RSLAP_RC_HIDPP;
    snprintf(d->ifname, sizeof(d->ifname), "%.60s", ifname);
    return RSLAP_RC_OK;
}

int g300s_claim(struct g300s_usbdev *d)
{
    struct usbdevfs_disconnect_claim dc;

    memset(&dc, 0, sizeof(dc));
    dc.interface = 1;
    if (ioctl(d->fd, USBDEVFS_DISCONNECT_CLAIM, &dc) < 0)
        return RSLAP_RC_HIDPP;
    return RSLAP_RC_OK;
}

/* One raw USB control transfer (class requests to interface 1). */
static int usb_ctrl(struct g300s_usbdev *d, uint8_t bm, uint8_t req,
                    uint16_t wval, uint16_t widx, uint8_t *data, uint16_t len)
{
    struct usbdevfs_ctrltransfer tr;
    int rc;

    memset(&tr, 0, sizeof(tr));
    tr.bRequestType = bm;
    tr.bRequest = req;
    tr.wValue = wval;
    tr.wIndex = widx;
    tr.wLength = len;
    tr.timeout = 1000; /* ms */
    tr.data = data;
    rc = ioctl(d->fd, USBDEVFS_CONTROL, &tr);
    if (rc < 0)
        return RSLAP_RC_HIDPP;
    return rc;
}

int g300s_load_mode(struct g300s_usbdev *d, int mode,
                    uint8_t raw[G300S_BLOCK_LEN])
{
    uint8_t rid;
    int rc;

    if (mode < 0 || mode >= G300S_MODE_COUNT)
        return RSLAP_RC_MALFORMED;

    rid = (uint8_t)(G300S_RID_F3 + mode);
    memset(raw, 0, G300S_BLOCK_LEN);
    rc = usb_ctrl(d, 0xa1, 0x01 /* GET_REPORT */,
                  (uint16_t)(0x0300 | rid), 1, raw, G300S_BLOCK_LEN);
    if (rc != G300S_BLOCK_LEN)
        return RSLAP_RC_MALFORMED;
    if (raw[0] != rid)
        return RSLAP_RC_MALFORMED;
    return RSLAP_RC_OK;
}

/* krayon's mouse_editmode(), verbatim flow: the five SET_REPORT
 * commands their tool runs before every mode read, with their pauses.
 * Probes read real data without it, but the reference never does —
 * and our tool's reads without it have only ever returned zeros.
 * Rejoin the reference flow (owner round decides if it is required). */
int g300s_editor_session(struct g300s_usbdev *d)
{
    static const uint8_t cmds[5][4] = {
        { 0xf0, 0x42, 0x39, 0x00 },  /* launch editor */
        { 0xf2, 0x4f, 0x00, 0x00 },  /* commit */
        { 0xf0, 0x00, 0x00, 0x00 },  /* leave */
        { 0xf1, 0x00, 0x00, 0x00 },  /* firmware reset-ish */
        { 0xf0, 0x42, 0x00, 0x00 },  /* start edit */
    };
    static const int lens[5] = { 4, 2, 4, 2, 4 };
    int i, rc;

    for (i = 0; i < 5; i++) {
        uint8_t buf[4];

        memcpy(buf, cmds[i], sizeof(buf));
        rc = usb_ctrl(d, 0x21, 0x09 /* SET_REPORT */,
                      (uint16_t)(0x0300 | cmds[i][0]), 1, buf,
                      (uint16_t)lens[i]);
        if (rc != lens[i])
            return RSLAP_RC_HIDPP;
        usleep(50000);
    }
    usleep(500000); /* the firmware settles, per the reference flow */
    return RSLAP_RC_OK;
}

void g300s_close(struct g300s_usbdev *d)
{
    unsigned int ifno = 1;
    FILE *f;

    if (d->fd >= 0) {
        ioctl(d->fd, USBDEVFS_RELEASEINTERFACE, &ifno); /* best effort */
        close(d->fd);
        d->fd = -1;
    }
    /* hand the interface back to usbhid so its hidraw node returns */
    f = fopen("/sys/bus/usb/drivers/usbhid/bind", "w");
    if (f) {
        fprintf(f, "%s", d->ifname);
        fclose(f);
    }
}

void g300s_decode(const uint8_t raw[G300S_BLOCK_LEN], struct g300s_mode *m)
{
    int i;

    memset(m, 0, sizeof(*m));
    memcpy(m->raw, raw, G300S_BLOCK_LEN);

    m->colour = raw[1];
    m->report_rate = raw[2] < 4 ? rate_table[raw[2]] : 0;

    for (i = 0; i < G300S_DPI_STAGE_COUNT; i++) {
        uint8_t b = raw[3 + i];

        m->dpi[i] = dpi_point(b & 0x0f);
        if (b & 0x80)
            m->dpi_default = i + 1;
    }

    m->dpi_shift = (raw[7] & 0x40) ? 0 : dpi_point(raw[7] & 0x0f);

    for (i = 0; i < G300S_BUTTON_COUNT; i++) {
        m->buttons[i].btn = raw[8 + 3 * i];
        m->buttons[i].mods = raw[9 + 3 * i];
        m->buttons[i].key = raw[10 + 3 * i];
    }
}

const char *g300s_colour_name(int idx)
{
    if (idx < 0 || idx >= 8)
        return "";
    return colour_table[idx];
}

const char *g300s_button_name(uint8_t idx)
{
    return button_table[idx & 0x0f];
}

const char *g300s_key_name(uint8_t usage)
{
    size_t i;

    for (i = 0; i < sizeof(key_table) / sizeof(key_table[0]); i++)
        if (key_table[i].usage == usage)
            return key_table[i].name;
    return "";
}

void g300s_mods_str(uint8_t mods, char *out, size_t outlen)
{
    int i, pos = 0;

    out[0] = '\0';
    for (i = 0; i < 8; i++) {
        if (!(mods & (uint8_t)(1 << i)))
            continue;
        if (pos)
            pos += snprintf(out + pos, outlen - (size_t)pos, " + ");
        if ((size_t)pos >= outlen)
            return;
        pos += snprintf(out + pos, outlen - (size_t)pos, "%s", mod_table[i]);
        if ((size_t)pos >= outlen)
            return;
    }
}
