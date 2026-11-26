/* ratslap-g305x — Logitech G300/G300s (046d:c246) support.
 *
 * The G300s predates HID++: it is configured through vendor feature
 * reports (IDs 0xF0-0xF5) on its second USB interface. Mode reads only
 * answer inside the device's editor session: krayon/ratslap (GPL-2)
 * runs the 0xF0 sequence before every mode_load, and the LGS sniff
 * shows the same. Without it the interface returns 35 zero bytes
 * (owner-confirmed on a mouse with configured profiles).
 * Protocol per krayon/ratslap (GPL-2) observations, re-implemented
 * here; names and layouts below are device facts, not copied code.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RSLAP_G300S_H
#define RSLAP_G300S_H

#include <stddef.h>
#include <stdint.h>

#define G300S_VID 0x046d
#define G300S_PID 0xc246

#define G300S_MODE_COUNT 3       /* modes live in reports 0xF3..0xF5 */
#define G300S_BLOCK_LEN 35       /* report id + 34 bytes of mode data */
#define G300S_BUTTON_COUNT 9     /* 3 bindings each, offsets 8..34  */
#define G300S_DPI_STAGE_COUNT 4  /* offsets 3..6                     */

/* One decoded binding: mouse-button nibble, modifier mask, key usage. */
struct g300s_binding {
    uint8_t btn;  /* low nibble indexes g300s_button_name()  */
    uint8_t mods; /* bit i set = s_keys modifier 0xE0 + i     */
    uint8_t key;  /* HID usage id, 0 = none                   */
};

struct g300s_mode {
    uint8_t raw[G300S_BLOCK_LEN]; /* as read/written on the wire  */
    int colour;                   /* 0..7, see g300s_colour_name  */
    int report_rate;              /* Hz; 0 = unknown index         */
    int dpi[G300S_DPI_STAGE_COUNT];
    int dpi_default;              /* 1-based stage, 0 = none       */
    int dpi_shift;                /* DPI value, 0 = disabled       */
    struct g300s_binding buttons[G300S_BUTTON_COUNT];
};

/* ---- transport: raw usbfs control transfers ------------------------- */
/* The kernel's HID stack polling the config interface makes the G300s
 * firmware answer mode GETs with zero bytes (owner-confirmed against a
 * mouse with configured profiles), so hidraw cannot serve this device.
 * The transport instead claims USB interface 1 through usbfs (usbhid
 * detached for the session) and speaks the vendor feature reports as
 * raw control transfers — the reference tool's libusb carrier, without
 * the 2013 ceremony. The interface's hidraw node is absent while the
 * claim is held and is restored on close (release + driver rebind). */
struct g300s_usbdev {
    int fd;
    char ifname[64];   /* e.g. "1-4:1.1" — the usbhid rebind name */
};

/* Find the G300s on the USB tree. Fills node_out with the usbfs node
 * path (/dev/bus/usb/BBB/DDD) and if_out with the interface-1 sysfs
 * name. RSLAP_USB_SYSFS_PATH overrides /sys/bus/usb/devices. Returns
 * 1 when a G300s is present. */
int g300s_scan(char *node_out, size_t node_len, char *if_out, size_t if_len);

/* Open the USB node (O_RDWR) and remember the rebind name. errno stays
 * set on failure (EACCES = udev rule missing). */
int g300s_open(struct g300s_usbdev *d, const char *node, const char *ifname);

/* Atomically detach usbhid from interface 1 and claim it for this fd
 * (USBDEVFS_DISCONNECT_CLAIM). Until release, the firmware serves real
 * mode data. */
int g300s_claim(struct g300s_usbdev *d);

/* Read one mode (0..2 -> report 0xF3..0xF5) into raw. No session
 * handshake is needed for reads (owner-confirmed: identical bytes with
 * and without the 0xF0 editor sequence). The report id must be echoed
 * as raw[0]. */
int g300s_load_mode(struct g300s_usbdev *d, int mode,
                    uint8_t raw[G300S_BLOCK_LEN]);

/* Release the claim, rebind usbhid, close. Restores the hidraw node. */
void g300s_close(struct g300s_usbdev *d);

/* Decode a 35-byte block. Never fails; unknown table indices decode
 * to 0/NULL-name placeholders so info stays printable. */
void g300s_decode(const uint8_t raw[G300S_BLOCK_LEN], struct g300s_mode *m);

/* Name tables (empty string when unknown). */
const char *g300s_colour_name(int idx);
const char *g300s_button_name(uint8_t idx);
const char *g300s_key_name(uint8_t usage);
/* "LeftCtrl + LeftShift" style rendering of the modifier mask. */
void g300s_mods_str(uint8_t mods, char *out, size_t outlen);

#endif /* RSLAP_G300S_H */
