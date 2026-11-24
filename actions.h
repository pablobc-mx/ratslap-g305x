/* ratslap-g305x — 4-byte on-board binding records: parse CLI tokens,
 * describe raw records, print the key catalog.
 *
 * Encoding follows OpenGMice (GPL-3.0, github.com/sabam54/OpenGMice):
 *   mouse button : 80 01 mask_be16
 *   key/modifier : 80 02 modifier_bitmask hid_usage
 *   media key    : 80 03 usage_be16
 *   function     : 90 code 00 00
 *   disabled     : FF FF FF FF
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RSLAP_ACTIONS_H
#define RSLAP_ACTIONS_H

#include "util.h"

/* Parse a binding token (see --listkeys). Returns RSLAP_RC_OK or
 * RSLAP_RC_MALFORMED for an unknown token. */
int rslap_parse_binding(const char *token, uint8_t out[4]);

/* Human-readable name for a raw record ("Left Click", "Ctrl+Shift+Z",
 * "Next Track", "DPI Cycle", "Disabled", "Key 0x2C"...). */
void rslap_describe_binding(const uint8_t raw[4], char *out, size_t outlen);

/* Print the full binding catalog, grouped, one token per line. */
void rslap_print_keylist(FILE *f);

#endif /* RSLAP_ACTIONS_H */
