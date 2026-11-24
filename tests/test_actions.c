/* Binding token parse / raw record describe tests.
 *
 * Encoding parity with OpenGMice's assignment options:
 *   80 01 mask_be16  mouse button
 *   80 02 mods usage key with modifiers (USB HID usage codes)
 *   80 03 usage_be16 media/consumer key
 *   90 code 00 00    special function (DPI, profiles, G-shift, ...)
 *   FF FF FF FF      disabled */
#include "actions.h"
#include <string.h>

static int fails = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                                    \
        }                                                               \
    } while (0)

static void check_parse(const char *tok, const uint8_t want[4])
{
    uint8_t raw[4] = {0};
    int rc = rslap_parse_binding(tok, raw);

    if (rc != RSLAP_RC_OK || memcmp(raw, want, 4) != 0)
        fprintf(stderr, "  (token '%s' -> rc=%d %02X %02X %02X %02X)\n",
                tok, rc, raw[0], raw[1], raw[2], raw[3]);
    CHECK(rc == RSLAP_RC_OK);
    CHECK(memcmp(raw, want, 4) == 0);
}

int main(void)
{
    uint8_t raw[4];
    char desc[64];

    /* ---- mouse buttons ---- */
    check_parse("left",   (const uint8_t[4]){0x80, 0x01, 0x00, 0x01});
    check_parse("right",  (const uint8_t[4]){0x80, 0x01, 0x00, 0x02});
    check_parse("middle", (const uint8_t[4]){0x80, 0x01, 0x00, 0x04});
    check_parse("back",   (const uint8_t[4]){0x80, 0x01, 0x00, 0x08});
    check_parse("forward", (const uint8_t[4]){0x80, 0x01, 0x00, 0x10});
    check_parse("mouse:0x0020", (const uint8_t[4]){0x80, 0x01, 0x00, 0x20});

    /* ---- disable ---- */
    check_parse("disable", (const uint8_t[4]){0xFF, 0xFF, 0xFF, 0xFF});

    /* ---- keys, plain and with modifiers ---- */
    check_parse("q", (const uint8_t[4]){0x80, 0x02, 0x00, 0x14});
    check_parse("5", (const uint8_t[4]){0x80, 0x02, 0x00, 0x22});
    check_parse("enter", (const uint8_t[4]){0x80, 0x02, 0x00, 0x28});
    check_parse("f5", (const uint8_t[4]){0x80, 0x02, 0x00, 0x3E});
    check_parse("f13", (const uint8_t[4]){0x80, 0x02, 0x00, 0x68});
    check_parse("ctrl+c", (const uint8_t[4]){0x80, 0x02, 0x01, 0x06});
    check_parse("ctrl+shift+z", (const uint8_t[4]){0x80, 0x02, 0x03, 0x1D});
    check_parse("alt+tab", (const uint8_t[4]){0x80, 0x02, 0x04, 0x2B});
    check_parse("super+d", (const uint8_t[4]){0x80, 0x02, 0x08, 0x07});
    check_parse("altgr+g", (const uint8_t[4]){0x80, 0x02, 0x40, 0x0A});
    check_parse("rctrl+f1", (const uint8_t[4]){0x80, 0x02, 0x10, 0x3A});
    check_parse("rshift+q", (const uint8_t[4]){0x80, 0x02, 0x20, 0x14});
    check_parse("rsuper+l", (const uint8_t[4]){0x80, 0x02, 0x80, 0x0F});
    check_parse("key:0x2c", (const uint8_t[4]){0x80, 0x02, 0x00, 0x2C});
    check_parse("ctrl+key:0x46", (const uint8_t[4]){0x80, 0x02, 0x01, 0x46});
    check_parse("ctrl+shift+alt+super+q",
                (const uint8_t[4]){0x80, 0x02, 0x0F, 0x14});

    /* ---- media keys ---- */
    check_parse("media:playpause", (const uint8_t[4]){0x80, 0x03, 0x00, 0xCD});
    check_parse("media:next", (const uint8_t[4]){0x80, 0x03, 0x00, 0xB5});
    check_parse("media:prev", (const uint8_t[4]){0x80, 0x03, 0x00, 0xB6});
    check_parse("media:mute", (const uint8_t[4]){0x80, 0x03, 0x00, 0xE2});
    check_parse("media:volup", (const uint8_t[4]){0x80, 0x03, 0x00, 0xE9});
    check_parse("media:voldown", (const uint8_t[4]){0x80, 0x03, 0x00, 0xEA});
    check_parse("media:calculator", (const uint8_t[4]){0x80, 0x03, 0x01, 0x92});
    check_parse("media:home", (const uint8_t[4]){0x80, 0x03, 0x02, 0x23});
    check_parse("media:browserback", (const uint8_t[4]){0x80, 0x03, 0x02, 0x24});
    check_parse("media:0x0221", (const uint8_t[4]){0x80, 0x03, 0x02, 0x21});

    /* ---- special functions ---- */
    check_parse("scrollleft", (const uint8_t[4]){0x90, 0x01, 0x00, 0x00});
    check_parse("scrollright", (const uint8_t[4]){0x90, 0x02, 0x00, 0x00});
    check_parse("dpiup", (const uint8_t[4]){0x90, 0x03, 0x00, 0x00});
    check_parse("dpidown", (const uint8_t[4]){0x90, 0x04, 0x00, 0x00});
    check_parse("dpicycle", (const uint8_t[4]){0x90, 0x05, 0x00, 0x00});
    check_parse("dpidefault", (const uint8_t[4]){0x90, 0x06, 0x00, 0x00});
    check_parse("dpishift", (const uint8_t[4]){0x90, 0x07, 0x00, 0x00});
    check_parse("profilenext", (const uint8_t[4]){0x90, 0x08, 0x00, 0x00});
    check_parse("profileprev", (const uint8_t[4]){0x90, 0x09, 0x00, 0x00});
    check_parse("profilecycle", (const uint8_t[4]){0x90, 0x0A, 0x00, 0x00});
    check_parse("gshift", (const uint8_t[4]){0x90, 0x0B, 0x00, 0x00});
    check_parse("batterystatus", (const uint8_t[4]){0x90, 0x0C, 0x00, 0x00});

    /* ---- unknown tokens must be refused ---- */
    CHECK(rslap_parse_binding("banana", raw) == RSLAP_RC_MALFORMED);
    CHECK(rslap_parse_binding("ctrl+", raw) == RSLAP_RC_MALFORMED);
    CHECK(rslap_parse_binding("", raw) == RSLAP_RC_MALFORMED);
    CHECK(rslap_parse_binding("media:badname", raw) == RSLAP_RC_MALFORMED);
    CHECK(rslap_parse_binding("key", raw) == RSLAP_RC_MALFORMED);
    CHECK(rslap_parse_binding("ctrl++c", raw) == RSLAP_RC_MALFORMED);
    CHECK(rslap_parse_binding("LEFT", raw) == RSLAP_RC_MALFORMED); /* case sensitive */

    /* ---- describe ---- */
    rslap_describe_binding((const uint8_t[4]){0x80, 0x01, 0x00, 0x01}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Left Click") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x01, 0x00, 0x04}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Middle Click") == 0);
    rslap_describe_binding((const uint8_t[4]){0xFF, 0xFF, 0xFF, 0xFF}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Disabled") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x02, 0x03, 0x1D}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Ctrl+Shift+Z") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x02, 0x00, 0x2C}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Space") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x02, 0x05, 0x14}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Ctrl+Alt+Q") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x03, 0x00, 0xB5}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Next Track") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x03, 0x00, 0xCD}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Play / Pause") == 0);
    rslap_describe_binding((const uint8_t[4]){0x90, 0x05, 0x00, 0x00}, desc, sizeof(desc));
    CHECK(strcmp(desc, "DPI Cycle") == 0);
    rslap_describe_binding((const uint8_t[4]){0x90, 0x07, 0x00, 0x00}, desc, sizeof(desc));
    CHECK(strcmp(desc, "DPI Shift") == 0);
    rslap_describe_binding((const uint8_t[4]){0x90, 0x0B, 0x00, 0x00}, desc, sizeof(desc));
    CHECK(strcmp(desc, "G-Shift") == 0);
    rslap_describe_binding((const uint8_t[4]){0x90, 0x0C, 0x00, 0x00}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Battery Status") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x02, 0x00, 0x99}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Key 0x99") == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x03, 0x01, 0x00}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Media 0x0100") == 0);
    rslap_describe_binding((const uint8_t[4]){0x90, 0x0D, 0x00, 0x00}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Function 0x0D") == 0);
    rslap_describe_binding((const uint8_t[4]){0x00, 0x01, 0x02, 0x03}, desc, sizeof(desc));
    CHECK(strncmp(desc, "Macro", 5) == 0);
    rslap_describe_binding((const uint8_t[4]){0x80, 0x01, 0x00, 0x40}, desc, sizeof(desc));
    CHECK(strcmp(desc, "Mouse Button 7") == 0); /* single-bit mask: button number */

    /* --listkeys: must contain every token family */
    {
        char *buf = NULL;
        size_t n = 0;
        FILE *f = open_memstream(&buf, &n);

        CHECK(f != NULL);
        if (f) {
            rslap_print_keylist(f);
            fclose(f);
            CHECK(n > 0 && n < (1u << 20)); /* sane size, non-empty */
            CHECK(strstr(buf, "media:playpause") != NULL);
            CHECK(strstr(buf, "dpicycle") != NULL);
            CHECK(strstr(buf, "disable") != NULL);
            CHECK(strstr(buf, "gshift") != NULL);
            CHECK(strstr(buf, "ctrl+") != NULL);
            CHECK(strstr(buf, "key:0x") != NULL);
            free(buf);
        }
    }

    if (fails) {
        fprintf(stderr, "test_actions: %d check(s) failed\n", fails);
        return 1;
    }
    puts("test_actions: OK");
    return 0;
}
