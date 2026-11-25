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

/* Open a G300s config node (O_RDWR). Returns the fd or -1 with errno
 * set (EACCES = uaccess/rule problem, ENOENT = node vanished). */
int g300s_open(const char *node);

/* Check an open fd: asks for mode report 0xF3 and expects the report
 * id echoed. Returns 1 if this node speaks the G300s config protocol,
 * 0 otherwise. Safe on non-G300s nodes: the kernel rejects feature
 * requests that are not in the report descriptor before any device
 * traffic happens. */
int g300s_check(int fd);

/* Read one mode (0..2 -> report 0xF3..0xF5) into raw. The report id
 * must be echoed as raw[0]; anything else is RSLAP_RC_MALFORMED. */
int g300s_load_mode(int fd, int mode, uint8_t raw[G300S_BLOCK_LEN]);

/* Open the device's editor session. The G300s firmware answers mode
 * GETs with zero bytes outside this state, so every read or write
 * session must begin here. Commands and pauses mirror the reference
 * flow (launch editor, commit, leave, the firmware's reset-ish 0xF1,
 * start-edit). The LED may dim for the session's duration — that is
 * the device's normal config-mode behaviour. */
int g300s_begin_session(int fd);

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
