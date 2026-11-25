/* Unit tests for the G300s mode-block decode (no hardware needed).
 *
 * Vector 1 is krayon's annotated example
 *   "F5040302 84060844 01000002 00000300 00040000 05000006 00000700 00080000 090000"
 * which documents the wired layout. Vector 2 is synthetic and covers
 * the fields the sample leaves at defaults.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "g300s.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static void test_krayon_example(void)
{
    static const uint8_t raw[G300S_BLOCK_LEN] = {
        0xf5, 0x04, 0x03, 0x02, 0x84, 0x06, 0x08, 0x44,
        0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x05, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x08, 0x00,
        0x00, 0x09, 0x00
    };
    struct g300s_mode m;

    g300s_decode(raw, &m);

    CHECK(m.colour == 4, "colour index 4");
    CHECK(strcmp(g300s_colour_name(m.colour), "blue") == 0, "colour 4 is blue");
    CHECK(m.report_rate == 500, "rate index 3 -> 500 Hz");
    CHECK(m.dpi[0] == 500, "dpi1 nibble 2 -> 500");
    CHECK(m.dpi[1] == 1000 && m.dpi_default == 2, "dpi2 0x84 -> 1000, default");
    CHECK(m.dpi[2] == 1500, "dpi3 nibble 6 -> 1500");
    CHECK(m.dpi[3] == 2000, "dpi4 nibble 8 -> 2000");
    CHECK(m.dpi_shift == 0, "0x44 -> shift disabled");

    CHECK(m.buttons[0].btn == 1, "b1 -> Button1");
    CHECK(strcmp(g300s_button_name(m.buttons[0].btn), "Button1") == 0,
          "b1 name");
    CHECK(m.buttons[4].btn == 0 && m.buttons[4].mods == 0x05,
          "b5 filler byte 05 lives in the modifier slot, btn is NONE");
    CHECK(strcmp(g300s_button_name(m.buttons[4].btn), "NONE") == 0,
          "b5 btn NONE");
    CHECK(strcmp(g300s_button_name(m.buttons[6].btn), "NONE") == 0 &&
          m.buttons[6].mods == 0x07,
          "b7 filler byte 07 lives in the modifier slot");
    CHECK(strcmp(g300s_button_name(m.buttons[7].btn), "NONE") == 0 &&
          m.buttons[7].mods == 0x08,
          "b8 filler byte 08 lives in the modifier slot");
    CHECK(m.buttons[8].btn == 0 && m.buttons[8].mods == 0x09,
          "b9 filler byte 09 lives in the modifier slot");

    CHECK(m.raw[0] == 0xf5, "raw block preserved");
}

static void test_synthetic(void)
{
    uint8_t raw[G300S_BLOCK_LEN];
    struct g300s_mode m;
    char mods[64];

    memset(raw, 0, sizeof(raw));
    raw[0] = 0xf4;
    raw[1] = 0x07;               /* white                     */
    raw[2] = 0x00;               /* rate index 0 -> 1000 Hz   */
    raw[3] = 0x0a;               /* 2500                      */
    raw[4] = 0x0f;               /* 3750                      */
    raw[5] = 0x80;               /* 4000, default stage 3     */
    raw[6] = 0x01;               /* 250                       */
    raw[7] = 0x03;               /* shift enabled at 750      */
    raw[8 + 3 * 8] = 0x0d;       /* b9 = ModeSwitch           */
    raw[9 + 3 * 8] = 0x05;       /* LeftCtrl | LeftAlt        */
    raw[10 + 3 * 8] = 0x19;      /* key V                     */

    g300s_decode(raw, &m);

    CHECK(strcmp(g300s_colour_name(m.colour), "white") == 0, "colour 7 white");
    CHECK(m.report_rate == 1000, "rate index 0 -> 1000 Hz");
    CHECK(m.dpi[0] == 2500 && m.dpi[1] == 3750, "dpi nibble points");
    CHECK(m.dpi[2] == 4000 && m.dpi_default == 3, "nibble 0 -> 4000, default");
    CHECK(m.dpi[3] == 250, "dpi nibble 1 -> 250");
    CHECK(m.dpi_shift == 750, "shift 0x03 -> 750 (enabled)");

    CHECK(strcmp(g300s_button_name(m.buttons[8].btn), "ModeSwitch") == 0,
          "nibble 13 -> ModeSwitch");
    CHECK(strcmp(g300s_key_name(m.buttons[8].key), "V") == 0, "usage 0x19 -> V");
    g300s_mods_str(m.buttons[8].mods, mods, sizeof(mods));
    CHECK(strcmp(mods, "LeftCtrl + LeftAlt") == 0, "mods 0x05 rendering");

    CHECK(g300s_button_name(0x2f)[0] == 'D', "0x2f masks to DPIDefault");
    CHECK(g300s_key_name(0x99)[0] == '\0', "unknown usage -> empty name");
    CHECK(g300s_colour_name(9)[0] == '\0', "unknown colour -> empty name");
}

int main(void)
{
    test_krayon_example();
    test_synthetic();

    if (failures) {
        printf("test_g300s: %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("test_g300s: all checks passed\n");
    return 0;
}
