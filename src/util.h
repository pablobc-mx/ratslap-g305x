/* ratslap-g305x — shared helpers.
 *
 * Every source file includes this first: it sets the feature-test macros
 * before any system header and defines the small byte/exit helpers.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RSLAP_UTIL_H
#define RSLAP_UTIL_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* Exit codes (documented in README.md). */
enum {
    EX_OK = 0,
    EX_NO_DEVICE = 1,  /* no compatible device found */
    EX_DEVICE = 2,     /* device I/O or protocol error */
    EX_USAGE = 3,      /* bad command line */
    EX_REFUSED = 4,    /* safety gate: destructive command without backup/--yes */
};

/* Transport return codes (negative on failure). */
enum rslap_rc {
    RSLAP_RC_OK = 0,
    RSLAP_RC_TIMEOUT = -1,
    RSLAP_RC_HIDPP = -2,     /* device returned an HID++ error (see dev->err) */
    RSLAP_RC_MALFORMED = -3, /* unreadable reply, size mismatch, bad CRC */
    RSLAP_RC_REFUSED = -4,   /* local sanity check refused the operation */
    RSLAP_RC_UNSUPPORTED = -5, /* device lacks the needed feature */
};

/* Print "ratslap-g305x: <msg>" to stderr and exit with `code`. */
#if defined(__GNUC__)
void die(int code, const char *fmt, ...) __attribute__((noreturn));
#else
void die(int code, const char *fmt, ...);
#endif

static inline uint16_t rslap_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void rslap_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static inline uint16_t rslap_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

#endif /* RSLAP_UTIL_H */
