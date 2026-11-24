/* ratslap-g305x — HID++ 2.0 feature 0x8100 (on-board profiles): sector
 * read/write with CRC-CCITT, profile decode/encode for the v1 (formats 1-5)
 * and v6 (formats 6-8) sector layouts, profile directory, mode switching.
 *
 * Layout verified against a real format-7 G305 X by OpenGMice
 * (GPL-3.0, github.com/sabam54/OpenGMice), which takes precedence over
 * libratbag where the two disagree.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RSLAP_ONBOARD_H
#define RSLAP_ONBOARD_H

#include "hidpp.h"

#define RSLAP_MAX_SECTOR 4096
#define RSLAP_DPI_SLOTS 5
#define RSLAP_BUTTON_SLOTS 16
#define RSLAP_MAX_BUTTONS 16
#define RSLAP_ROM_BASE 0x0100
#define RSLAP_MODE_ONBOARD 1
#define RSLAP_MODE_HOST 2

/* 0x8100 function numbers. */
#define OB_FN_SET_MODE 1
#define OB_FN_GET_MODE 2
#define OB_FN_SET_SECTOR 3
#define OB_FN_GET_SECTOR 4
#define OB_FN_READ 5
#define OB_FN_WRITE_START 6
#define OB_FN_WRITE_DATA 7
#define OB_FN_WRITE_END 8
#define OB_FN_GET_DPI_IDX 0x0B
#define OB_FN_SET_DPI_IDX 0x0C

#define RSLAP_N_RATES 7
/* Report rates indexed by the v6 rate byte. */
extern const int rslap_report_rates[RSLAP_N_RATES];

/* CRC-CCITT (poly 0x1021, init 0xFFFF, msb-first, no reflection) — the exact
 * variant the mouse firmware uses; canonical check value for
 * "123456789" is 0x29B1. */
int rslap_crc_ccitt(const uint8_t *data, size_t len);
/* Write the big-endian CRC of data[0..len-2] into data[len-2..len-1]. */
void rslap_with_crc(uint8_t *data, size_t len);
int rslap_crc_ok(const uint8_t *data, size_t len);

struct rslap_layout {
    int rate_wireless; /* offset of the wireless rate byte (== rate_wired on v1) */
    int rate_wired;
    int dpi;           /* offset of [default idx, shift idx, stages...] */
    int stage_size;    /* 5 (v6: X le16, Y le16, LOD) or 2 (v1: X le16) */
    int ps_timeout;    /* le16 sleep timeout offset */
    int po_timeout;    /* le16 power-off timeout offset */
    int buttons;       /* 16 x 4-byte binding records */
    int gshift;        /* G-shift binding records, same encoding */
    int name;          /* 48-byte UTF-16LE profile name */
    int rate_is_index; /* v6: byte indexes rslap_report_rates; v1: 1000/hz */
};

const struct rslap_layout *rslap_layout_for(int fmt);
/* hz -> v6 index, -1 if not in the table. */
int rslap_rate_index(int hz);
/* v6 index -> Hz (1000 for anything out of range). */
int rslap_rate_from_index(int idx);

struct rslap_dpi_stage {
    int x, y, lod;
};

/* Decoded view of one profile sector; raw[] carries every byte so unknown
 * fields survive a decode/encode round trip untouched. */
struct rslap_profile {
    int sector, fmt;
    uint8_t raw[RSLAP_MAX_SECTOR];
    size_t raw_len;
    int enabled;
    int report_rate;       /* Hz, wireless */
    int report_rate_wired; /* Hz */
    int default_dpi;       /* 0-based stage index */
    int shift_dpi;         /* 0-based stage index */
    struct rslap_dpi_stage stages[RSLAP_DPI_SLOTS];
    int n_stages;
    uint8_t buttons[RSLAP_BUTTON_SLOTS][4];
    uint8_t gshift_buttons[RSLAP_BUTTON_SLOTS][4];
    int n_buttons;
    char name[48]; /* UTF-16LE decoded to UTF-8-ish ASCII; "" when unset */
    int ps_timeout, po_timeout;
};

/* Returns RSLAP_RC_OK or RSLAP_RC_MALFORMED when raw is too short. */
int rslap_profile_decode(struct rslap_profile *p, const uint8_t *raw,
                         size_t len, int sector, int fmt, int n_buttons);
/* Re-encode p (raw copy + known fields overlaid + fresh CRC) into out.
 * Returns the byte count or RSLAP_RC_REFUSED when outlen is too small. */
int rslap_profile_encode(const struct rslap_profile *p, uint8_t *out, size_t outlen);

/* ------------------------------------------------------------ 0x8100 ops */

struct rslap_ob_info {
    int memory_model, fmt, macro_fmt;
    int profile_count, profile_count_oob;
    int button_count, sector_count, sector_size; /* sector_size in bytes */
    int gshift;
};

struct rslap_onboard {
    struct hidpp_device *dev;
    struct rslap_ob_info info;
};

/* Resolve the 0x8100 feature index and read the info struct (fn 0).
 * Returns RSLAP_RC_OK or negative; RSLAP_RC_REFUSED for an unsupported
 * memory model. */
int rslap_ob_init(struct rslap_onboard *ob, struct hidpp_device *dev);

/* Read one sector (16-byte chunks; the final partial chunk is re-read so the
 * read ends exactly at the sector end). outlen must be >= sector_size.
 * Returns RSLAP_RC_OK or negative. */
int rslap_ob_read_sector(const struct rslap_onboard *ob, int sector,
                         uint8_t *out, size_t outlen);

/* Write one sector with full safety: refuses sectors >= RSLAP_ROM_BASE or
 * >= sector_count, refuses data whose CRC does not match, writes via
 * fn 6/7/8, then reads the sector back and compares byte for byte.
 * Returns RSLAP_RC_OK or negative. */
int rslap_ob_write_sector(const struct rslap_onboard *ob, int sector,
                          const uint8_t *data, size_t len);

int rslap_ob_get_mode(const struct rslap_onboard *ob);
int rslap_ob_set_mode(const struct rslap_onboard *ob, int mode);
int rslap_ob_get_current_sector(const struct rslap_onboard *ob);
int rslap_ob_set_current_sector(const struct rslap_onboard *ob, int sector);

struct rslap_dir_entry {
    int sector, enabled;
};

/* Read the profile directory (sector 0, or the ROM directory at 0x0100 when
 * rom != 0). Returns entry count or negative. */
int rslap_ob_read_directory(const struct rslap_onboard *ob, int rom,
                            struct rslap_dir_entry *out, int max);
int rslap_ob_write_directory(const struct rslap_onboard *ob,
                             const struct rslap_dir_entry *entries, int n);
/* ROM profile sectors from the ROM directory (fallback: [0x0101]). */
int rslap_ob_rom_sectors(const struct rslap_onboard *ob, int *out, int max);

/* Make sector the active profile; when it already is, hop to another
 * enabled profile first so the firmware re-applies it (best effort). */
int rslap_ob_activate(struct rslap_onboard *ob, int sector);

int rslap_ob_get_dpi_index(const struct rslap_onboard *ob);

#endif /* RSLAP_ONBOARD_H */
