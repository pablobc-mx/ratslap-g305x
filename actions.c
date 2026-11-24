/* Binding token parsing, raw-record description, key catalog.
 *
 * Record encodings and the function/media/key catalogs follow OpenGMice
 * (GPL-3.0, github.com/sabam54/OpenGMice), which verified them against
 * real G305-family hardware. */
#include "actions.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
/* ------------------------------------------------------------- catalogs */

struct mod {
    const char *name;
    uint8_t bit;
    const char *desc;
};

static const struct mod mods[] = {
    {"ctrl", 0x01, "Ctrl"},   {"shift", 0x02, "Shift"},
    {"alt", 0x04, "Alt"},     {"super", 0x08, "Super"},
    {"rctrl", 0x10, "RCtrl"}, {"rshift", 0x20, "RShift"},
    {"altgr", 0x40, "AltGr"}, {"rsuper", 0x80, "RSuper"},
};

struct mouse_btn {
    const char *name;
    uint16_t mask;
};

static const struct mouse_btn mouse_btns[] = {
    {"left", 0x0001}, {"right", 0x0002}, {"middle", 0x0004},
    {"back", 0x0008}, {"forward", 0x0010},
};

struct function {
    const char *token;
    uint8_t code;
    const char *desc;
};

static const struct function functions[] = {
    {"scrollleft", 0x01, "Scroll Left"},
    {"scrollright", 0x02, "Scroll Right"},
    {"dpiup", 0x03, "DPI Up"},
    {"dpidown", 0x04, "DPI Down"},
    {"dpicycle", 0x05, "DPI Cycle"},
    {"dpidefault", 0x06, "DPI Default"},
    {"dpishift", 0x07, "DPI Shift"},
    {"profilenext", 0x08, "Next Profile"},
    {"profileprev", 0x09, "Previous Profile"},
    {"profilecycle", 0x0A, "Cycle Profiles"},
    {"gshift", 0x0B, "G-Shift"},
    {"batterystatus", 0x0C, "Battery Status"},
};

struct consumer {
    const char *token;
    uint16_t usage;
    const char *desc;
};

static const struct consumer consumers[] = {
    {"playpause", 0x00CD, "Play / Pause"},
    {"next", 0x00B5, "Next Track"},
    {"prev", 0x00B6, "Previous Track"},
    {"stop", 0x00B7, "Stop"},
    {"mute", 0x00E2, "Mute"},
    {"volup", 0x00E9, "Volume Up"},
    {"voldown", 0x00EA, "Volume Down"},
    {"calculator", 0x0192, "Calculator"},
    {"mail", 0x018A, "Mail"},
    {"home", 0x0223, "Browser Home"},
    {"browserback", 0x0224, "Browser Back"},
    {"browserforward", 0x0225, "Browser Forward"},
    {"browserrefresh", 0x0227, "Browser Refresh"},
    {"search", 0x0221, "Search"},
    {"brightup", 0x006F, "Brightness Up"},
    {"brightdown", 0x0070, "Brightness Down"},
};

struct key_name {
    uint8_t usage;
    const char *token; /* CLI spelling */
    const char *label; /* human-readable (OpenGMice wording) */
};

/* USB HID usage codes (keyboard page) for the keys worth binding. */
static const struct key_name key_names[] = {
    {0x28, "enter", "Enter"}, {0x29, "esc", "Esc"},
    {0x2A, "backspace", "Backspace"}, {0x2B, "tab", "Tab"},
    {0x2C, "space", "Space"}, {0x2D, "minus", "-"}, {0x2E, "equal", "="},
    {0x2F, "bracketleft", "["}, {0x30, "bracketright", "]"},
    {0x31, "backslash", "\\"}, {0x33, "semicolon", ";"},
    {0x34, "apostrophe", "'"}, {0x35, "grave", "`"},
    {0x36, "comma", ","}, {0x37, "period", "."}, {0x38, "slash", "/"},
    {0x39, "capslock", "Caps Lock"},
    {0x46, "printscreen", "Print Screen"}, {0x47, "scrolllock", "Scroll Lock"},
    {0x48, "pause", "Pause"}, {0x49, "insert", "Insert"},
    {0x4A, "home", "Home"}, {0x4B, "pageup", "Page Up"},
    {0x4C, "delete", "Delete"}, {0x4D, "end", "End"},
    {0x4E, "pagedown", "Page Down"}, {0x4F, "right", "Right"},
    {0x50, "left", "Left"}, {0x51, "down", "Down"}, {0x52, "up", "Up"},
    {0x53, "numlock", "Num Lock"}, {0x54, "kpslash", "KP /"},
    {0x55, "kpasterisk", "KP *"}, {0x56, "kpminus", "KP -"},
    {0x57, "kpplus", "KP +"}, {0x58, "kpenter", "KP Enter"},
    {0x59, "kp1", "KP 1"}, {0x5A, "kp2", "KP 2"}, {0x5B, "kp3", "KP 3"},
    {0x5C, "kp4", "KP 4"}, {0x5D, "kp5", "KP 5"}, {0x5E, "kp6", "KP 6"},
    {0x5F, "kp7", "KP 7"}, {0x60, "kp8", "KP 8"}, {0x61, "kp9", "KP 9"},
    {0x62, "kp0", "KP 0"}, {0x65, "menu", "Menu"},
};

/* --------------------------------------------------------------- helpers */

static int parse_hex(const char *s, long *out)
{
    char *end;
    long v;

    if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
        return 0;
    v = strtol(s + 2, &end, 16);
    if (end == s + 2 || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

/* key name -> HID usage; -1 when unknown */
static int key_usage(const char *name)
{
    if (name[0] >= 'a' && name[0] <= 'z' && name[1] == '\0')
        return 0x04 + (name[0] - 'a');
    if (name[0] >= '1' && name[0] <= '9' && name[1] == '\0')
        return 0x1E + (name[0] - '1');
    if (name[0] == '0' && name[1] == '\0')
        return 0x27;
    if (name[0] == 'f') {
        char *end;
        long n = strtol(name + 1, &end, 10);

        if (end != name + 1 && *end == '\0' && n >= 1 && n <= 12)
            return 0x3A + (int)n - 1;
        if (end != name + 1 && *end == '\0' && n >= 13 && n <= 24)
            return 0x68 + (int)n - 13;
    }
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
        if (strcmp(name, key_names[i].token) == 0)
            return key_names[i].usage;
    return -1;
}

/* ------------------------------------------------------------------ parse */

int rslap_parse_binding(const char *token, uint8_t out[4])
{
    uint8_t mod_bits = 0;
    const char *p = token;
    int nmods = 0;
    long raw;

    if (token == NULL || token[0] == '\0')
        return RSLAP_RC_MALFORMED;

    /* disable / mouse buttons need no modifier stack */
    if (strcmp(token, "disable") == 0) {
        memset(out, 0xFF, 4);
        return RSLAP_RC_OK;
    }
    if (strncmp(token, "mouse:0x", 8) == 0) {
        if (!parse_hex(token + 6, &raw) || raw < 1 || raw > 0xFFFF)
            return RSLAP_RC_MALFORMED;
        out[0] = 0x80;
        out[1] = 0x01;
        out[2] = (uint8_t)(raw >> 8);
        out[3] = (uint8_t)raw;
        return RSLAP_RC_OK;
    }

    /* modifiers: zero or more name+ tokens, then the final key token */
    for (;;) {
        const char *plus = strchr(p, '+');
        size_t len = plus ? (size_t)(plus - p) : strlen(p);
        char head[24];
        int matched = 0;

        if (len == 0 || len >= sizeof(head))
            return RSLAP_RC_MALFORMED;
        memcpy(head, p, len);
        head[len] = '\0';

        /* mouse / media / function / raw forms terminate the stack */
        if (strcmp(head, "left") == 0 || strcmp(head, "right") == 0 ||
            strcmp(head, "middle") == 0 || strcmp(head, "back") == 0 ||
            strcmp(head, "forward") == 0) {
            if (mod_bits != 0 || plus != NULL)
                return RSLAP_RC_MALFORMED;
            for (size_t i = 0; i < sizeof(mouse_btns) / sizeof(mouse_btns[0]); i++)
                if (strcmp(head, mouse_btns[i].name) == 0) {
                    out[0] = 0x80;
                    out[1] = 0x01;
                    out[2] = (uint8_t)(mouse_btns[i].mask >> 8);
                    out[3] = (uint8_t)mouse_btns[i].mask;
                    return RSLAP_RC_OK;
                }
        }
        if (strncmp(head, "media:", 6) == 0) {
            if (mod_bits != 0 || plus != NULL)
                return RSLAP_RC_MALFORMED;
            if (parse_hex(head + 6, &raw) && raw >= 0 && raw <= 0xFFFF) {
                out[0] = 0x80;
                out[1] = 0x03;
                out[2] = (uint8_t)(raw >> 8);
                out[3] = (uint8_t)raw;
                return RSLAP_RC_OK;
            }
            for (size_t i = 0; i < sizeof(consumers) / sizeof(consumers[0]); i++)
                if (strcmp(head + 6, consumers[i].token) == 0) {
                    out[0] = 0x80;
                    out[1] = 0x03;
                    out[2] = (uint8_t)(consumers[i].usage >> 8);
                    out[3] = (uint8_t)consumers[i].usage;
                    return RSLAP_RC_OK;
                }
            return RSLAP_RC_MALFORMED;
        }
        for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
            if (strcmp(head, functions[i].token) == 0) {
                if (mod_bits != 0 || plus != NULL)
                    return RSLAP_RC_MALFORMED;
                out[0] = 0x90;
                out[1] = functions[i].code;
                out[2] = 0;
                out[3] = 0;
                return RSLAP_RC_OK;
            }
        }

        /* modifier? */
        for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
            if (strcmp(head, mods[i].name) == 0) {
                mod_bits |= mods[i].bit;
                matched = 1;
                break;
            }
        }
        if (matched) {
            nmods++;
            if (plus == NULL)
                return RSLAP_RC_MALFORMED; /* "ctrl+" with no key */
            p = plus + 1;
            if (nmods > 8)
                return RSLAP_RC_MALFORMED;
            continue;
        }

        /* final token: a key */
        if (plus != NULL)
            return RSLAP_RC_MALFORMED; /* unknown head mid-stack */
        if (strncmp(head, "key:0x", 6) == 0) {
            if (!parse_hex(head + 4, &raw) || raw < 0 || raw > 0xFF)
                return RSLAP_RC_MALFORMED;
            out[0] = 0x80;
            out[1] = 0x02;
            out[2] = mod_bits;
            out[3] = (uint8_t)raw;
            return RSLAP_RC_OK;
        }
        {
            int usage = key_usage(head);

            if (usage < 0)
                return RSLAP_RC_MALFORMED;
            out[0] = 0x80;
            out[1] = 0x02;
            out[2] = mod_bits;
            out[3] = (uint8_t)usage;
            return RSLAP_RC_OK;
        }
    }
}

/* --------------------------------------------------------------- describe */

static void describe_key_name(int usage, char *out, size_t outlen)
{
    if (usage >= 0x04 && usage <= 0x23)
        snprintf(out, outlen, "%c", 'A' + (usage - 0x04));
    else if (usage >= 0x1E && usage <= 0x26)
        snprintf(out, outlen, "%c", '1' + (usage - 0x1E));
    else if (usage == 0x27)
        snprintf(out, outlen, "0");
    else if (usage >= 0x3A && usage <= 0x45)
        snprintf(out, outlen, "F%d", usage - 0x3A + 1);
    else if (usage >= 0x68 && usage <= 0x73)
        snprintf(out, outlen, "F%d", usage - 0x68 + 13);
    else {
        for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
            if (key_names[i].usage == usage) {
                snprintf(out, outlen, "%s", key_names[i].label);
                return;
            }
        snprintf(out, outlen, "Key 0x%02X", usage);
    }
}

void rslap_describe_binding(const uint8_t raw[4], char *out, size_t outlen)
{
    uint16_t u;

    if (raw[0] == 0xFF) {
        snprintf(out, outlen, "Disabled");
        return;
    }

    switch (raw[0] >> 4) {
    case 0x8:
        switch (raw[1]) {
        case 0x00:
            snprintf(out, outlen, "Disabled");
            return;
        case 0x01:
            u = rslap_be16(raw + 2);
            if (u == 0x0001)
                snprintf(out, outlen, "Left Click");
            else if (u == 0x0002)
                snprintf(out, outlen, "Right Click");
            else if (u == 0x0004)
                snprintf(out, outlen, "Middle Click");
            else if (u == 0x0008)
                snprintf(out, outlen, "Back");
            else if (u == 0x0010)
                snprintf(out, outlen, "Forward");
            else if (u != 0 && (u & (u - 1)) == 0) {
                int bit = 0;
                for (int b = 0; b < 16; b++)
                    if (u & (1u << b)) {
                        bit = b + 1;
                        break;
                    }
                snprintf(out, outlen, "Mouse Button %d", bit);
            } else
                snprintf(out, outlen, "Buttons 0x%04X", u);
            return;
        case 0x02: {
            char key[24];
            char mods_str[80] = "";
            size_t o = 0;

            describe_key_name(raw[3], key, sizeof(key));
            for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
                if (raw[2] & mods[i].bit) {
                    int n = snprintf(mods_str + o, sizeof(mods_str) - o,
                                     "%s+", mods[i].desc);
                    if (n < 0 || o + (size_t)n >= sizeof(mods_str))
                        break;
                    o += (size_t)n;
                }
            }
            if (raw[3] == 0)
                snprintf(out, outlen, "%sNo key", mods_str);
            else
                snprintf(out, outlen, "%s%s", mods_str, key);
            return;
        }
        case 0x03:
            u = rslap_be16(raw + 2);
            for (size_t i = 0; i < sizeof(consumers) / sizeof(consumers[0]); i++)
                if (u == consumers[i].usage) {
                    snprintf(out, outlen, "%s", consumers[i].desc);
                    return;
                }
            snprintf(out, outlen, "Media 0x%04X", u);
            return;
        default:
            snprintf(out, outlen, "Unknown %02X %02X %02X %02X",
                     raw[0], raw[1], raw[2], raw[3]);
            return;
        }
    case 0x9:
        for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++)
            if (raw[1] == functions[i].code) {
                snprintf(out, outlen, "%s", functions[i].desc);
                return;
            }
        snprintf(out, outlen, "Function 0x%02X", raw[1]);
        return;
    case 0x0:
    case 0x1:
        snprintf(out, outlen, "Macro (onboard)");
        return;
    default:
        snprintf(out, outlen, "Unknown %02X %02X %02X %02X",
                 raw[0], raw[1], raw[2], raw[3]);
        return;
    }
}

/* -------------------------------------------------------------- key list */

void rslap_print_keylist(FILE *f)
{
    fprintf(f, "MOUSE:\n");
    for (size_t i = 0; i < sizeof(mouse_btns) / sizeof(mouse_btns[0]); i++)
        fprintf(f, "  %s\n", mouse_btns[i].name);
    fprintf(f, "  mouse:0xMASK   (raw button bitmask, e.g. mouse:0x0020)\n");

    fprintf(f, "DISABLE:\n  disable\n");

    fprintf(f, "FUNCTIONS:\n");
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++)
        fprintf(f, "  %s\n", functions[i].token);

    fprintf(f, "MEDIA:\n");
    for (size_t i = 0; i < sizeof(consumers) / sizeof(consumers[0]); i++)
        fprintf(f, "  media:%s\n", consumers[i].token);
    fprintf(f, "  media:0xUSAGE (raw consumer usage, e.g. media:0x0221)\n");

    fprintf(f, "MODIFIERS (prefix a key, combinable with +):\n");
    fprintf(f, "  ctrl+ shift+ alt+ super+ altgr+ rctrl+ rshift+ rsuper+\n");

    fprintf(f, "KEYS (a-z, 0-9 and below; raw form: key:0xNN):\n");
    fprintf(f, "  a b c d e f g h i j k l m n o p q r s t u v w x y z\n");
    fprintf(f, "  1 2 3 4 5 6 7 8 9 0\n");
    fprintf(f, "  ");
    for (int u = 0x3A; u <= 0x45; u++)
        fprintf(f, "f%d ", u - 0x3A + 1);
    fprintf(f, "\n  ");
    for (int u = 0x68; u <= 0x73; u++)
        fprintf(f, "f%d ", u - 0x68 + 13);
    fprintf(f, "\n");
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
        fprintf(f, "  %s\n", key_names[i].token);
    fprintf(f, "  key:0xNN      (raw HID usage code)\n");
}
