/* ratslap-g305x — CLI for the Logitech G305 X on-board memory.
 *
 * UX modeled on krayon/ratslap; protocol per OpenGMice (HID++ 2.0,
 * on-board profiles feature 0x8100, profile format 7).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "util.h"
#include "hidpp.h"
#include "onboard.h"
#include "actions.h"
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#define RSLAP_VERSION "0.1.0"
#define PROBE_MS 600

struct app {
    struct hidpp_device dev;
    struct rslap_onboard ob;
    struct hidpp_candidate cand;
    char name[128];
    char fw[48];
    int has_fw;
    uint8_t unit[4];
    int has_unit;
    uint16_t models[3];
};

void die(int code, const char *fmt, ...);

static void usage(FILE *f)
{
    fprintf(f,
        "ratslap-g305x %s — configure the Logitech G305 X (on-board memory)\n"
        "\n"
        "usage: ratslap-g305x [options] <command> [args]\n"
        "\n"
        "options:\n"
        "  -d, --device MATCH   pick a mouse by part of its name or product id\n"
        "      --yes            allow on-board writes without a prior backup\n"
        "  -h, --help           this help\n"
        "      --listkeys       list every binding token\n"
        "      --version\n"
        "\n"
        "commands:\n"
        "  (none)               print the active profile\n"
        "  list                 scan /dev/hidraw* and list compatible mice\n"
        "  info                 device + all profiles\n"
        "  backup FILE          save on-board memory to FILE (text)\n"
        "  restore FILE         write a backup back to the mouse\n"
        "  button N BINDING     set button N (1-%d) of the active profile\n"
        "  gbutton N BINDING    set G-shift button N\n"
        "  dpi LEVEL X [Y]      set DPI level (1-5) of the active profile\n"
        "  dpi --live X [Y]     live sensor preview (not saved)\n"
        "  rate HZ              report rate 125|250|500|1000 (wireless and wired)\n"
        "  activate N           make profile slot N the active one\n"
        "  reset [N]            copy the factory ROM profile over slot N\n"
        "\n"
        "exit codes: 0 ok · 1 no compatible device · 2 device/protocol error ·\n"
        "            3 usage · 4 refused (no backup, no --yes)\n",
        RSLAP_VERSION, RSLAP_MAX_BUTTONS);
}

/* ------------------------------------------------------------- utilities */

static void mkpath(const char *path)
{
    char tmp[512];
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0)
        return;
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static const char *state_dir(void)
{
    static char buf[512];
    const char *env = getenv("RSLAP_STATE_DIR");

    if (env && env[0])
        return env;
    env = getenv("XDG_STATE_HOME");
    if (env && env[0]) {
        snprintf(buf, sizeof(buf), "%s/ratslap-g305x", env);
        return buf;
    }
    env = getenv("HOME");
    if (env && env[0]) {
        snprintf(buf, sizeof(buf), "%s/.local/state/ratslap-g305x", env);
        return buf;
    }
    return NULL;
}

static void unit_hex(const uint8_t unit[4], char *out, size_t outlen)
{
    snprintf(out, outlen, "%02X%02X%02X%02X", unit[0], unit[1], unit[2], unit[3]);
}

static int rc_exit(int rc, const char *what)
{
    switch (rc) {
    case RSLAP_RC_OK:
        return 0;
    case RSLAP_RC_TIMEOUT:
        die(EX_DEVICE, "%s: no reply from the device (timeout)", what);
    case RSLAP_RC_HIDPP:
        die(EX_DEVICE, "%s: HID++ error", what);
    case RSLAP_RC_MALFORMED:
        die(EX_DEVICE, "%s: unreadable reply or verification failed", what);
    case RSLAP_RC_REFUSED:
        die(EX_DEVICE, "%s: refused (device or sanity check)", what);
    case RSLAP_RC_UNSUPPORTED:
        die(EX_DEVICE, "%s: unsupported by this device", what);
    default:
        die(EX_DEVICE, "%s: failed (code %d)", what, rc);
    }
    return EX_DEVICE;
}

/* ------------------------------------------------------------ safety gate */

static void gate_write(const struct app *a, int force)
{
    const char *dir;
    char path[600], key[16], line[512];
    FILE *f;

    if (force)
        return;
    dir = state_dir();
    if (!dir || !a->has_unit)
        goto refuse;
    unit_hex(a->unit, key, sizeof(key));
    snprintf(path, sizeof(path), "%s/unit-%s.last-backup", dir, key);
    f = fopen(path, "r");
    if (!f)
        goto refuse;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        goto refuse;
    }
    fclose(f);
    line[strcspn(line, "\n")] = '\0';
    if (line[0] != '/' || access(line, F_OK) != 0)
        goto refuse;
    return; /* a backup for this mouse exists on disk */
refuse:
    die(EX_REFUSED,
        "refusing to modify on-board memory without a backup\n"
        "  run 'ratslap-g305x backup FILE' first, or pass --yes to force");
}

static void remember_backup(const struct app *a, const char *file)
{
    const char *dir = state_dir();
    char path[600], key[16], resolved[4096];
    FILE *f;
    time_t t = time(NULL);
    char tbuf[32];

    if (!dir || !a->has_unit)
        return;
    mkpath(dir);
    unit_hex(a->unit, key, sizeof(key));
    snprintf(path, sizeof(path), "%s/unit-%s.last-backup", dir, key);
    if (!realpath(file, resolved))
        snprintf(resolved, sizeof(resolved), "%s", file);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%S%z", localtime(&t));
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "%s\n%s\n", resolved, tbuf);
    fclose(f);
}

/* -------------------------------------------------------------- discovery */

/* returns: 0 opened (a filled), -1 not compatible, -2 permission denied */
static int try_open(struct app *a, const struct hidpp_candidate *c)
{
    if (hidpp_open(&a->dev, c) != RSLAP_RC_OK)
        return errno == EACCES ? -2 : -1;
    a->cand = *c;
    a->dev.timeout_ms = PROBE_MS;
    if (hidpp_ping(&a->dev, NULL, NULL) != RSLAP_RC_OK)
        goto nope;
    if (hidpp_feature_index(&a->dev, FEAT_ONBOARD, F_ONBOARD_PROFILES) != RSLAP_RC_OK)
        goto nope; /* speaks HID++ but has no on-board profiles */
    a->dev.timeout_ms = 2000;

    /* compatible endpoint: gather identity */
    a->name[0] = '\0';
    if (hidpp_get_name(&a->dev, a->name, sizeof(a->name)) != RSLAP_RC_OK || !a->name[0])
        snprintf(a->name, sizeof(a->name), "%s", c->name);
    a->has_fw = hidpp_get_firmware(&a->dev, a->fw, sizeof(a->fw)) == RSLAP_RC_OK;
    a->has_unit = hidpp_get_unit(&a->dev, a->unit, a->models) == RSLAP_RC_OK;
    return 0;
nope:
    hidpp_close(&a->dev);
    return -1;
}

/* returns 0 with a->dev usable, or exits */
static void discover(struct app *a, const char *match)
{
    struct hidpp_candidate cands[64];
    int n = hidpp_scan(cands, 64);
    int saw_dev = 0, saw_deny = 0;

    for (int i = 0; i < n; i++) {
        int rc = try_open(a, &cands[i]);

        if (rc == -2) {
            saw_deny = 1;
            continue;
        }
        if (rc != 0)
            continue;
        saw_dev = 1;
        if (match) {
            char key[256], m[64];

            snprintf(key, sizeof(key), "%s %04x", a->name, a->dev.pid);
            for (size_t x = 0; key[x]; x++)
                key[x] = (char)tolower((unsigned char)key[x]);
            snprintf(m, sizeof(m), "%s", match);
            for (size_t x = 0; m[x]; x++)
                m[x] = (char)tolower((unsigned char)m[x]);
            if (!strstr(key, m)) {
                hidpp_close(&a->dev);
                continue;
            }
        }
        return; /* this one */
    }
    if (saw_deny)
        die(EX_NO_DEVICE,
            "found a Logitech HID++ device but no permission to open it\n"
            "  install the udev rule and replug (see README: 'Install the udev rule')");
    if (saw_dev)
        die(EX_NO_DEVICE, "no mouse with on-board profiles answered");
    die(EX_NO_DEVICE,
        "no compatible device found\n"
        "  hint: run 'lsusb | grep 046d' — the mouse or its receiver must be present\n"
        "  hint: if it is present but access fails, install 42-g305x.rules (README)");
}

static void open_app(struct app *a, const char *match)
{
    int rc;

    memset(a, 0, sizeof(*a));
    a->dev.fd = -1;
    discover(a, match);
    rc = rslap_ob_init(&a->ob, &a->dev);
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "reading on-board memory info");
}

/* ----------------------------------------------------------------- render */

static void print_profile(const struct rslap_onboard *ob, const struct rslap_profile *p,
                          int active)
{
    char desc[64];
    const char *tag;

    if (p->sector >= RSLAP_ROM_BASE)
        tag = "ROM default";
    else
        tag = p->enabled ? "slot" : "unused slot";
    printf("  %s — %s %d%s\n", p->name[0] ? p->name : "(unnamed)", tag, p->sector,
           active ? " (active)" : "");
    printf("    report rate: %d Hz wireless / %d Hz wired\n",
           p->report_rate, p->report_rate_wired);
    fputs("    DPI levels:", stdout);
    for (int i = 0; i < p->n_stages; i++) {
        char xbuf[32];

        snprintf(xbuf, sizeof(xbuf), "%s%d", i ? " " : " ", p->stages[i].x);
        if (p->stages[i].y != p->stages[i].x)
            snprintf(xbuf + strlen(xbuf), sizeof(xbuf) - strlen(xbuf),
                     "/%d", p->stages[i].y);
        fputs(xbuf, stdout);
        if (i == p->default_dpi)
            fputs("*", stdout);
    }
    printf("   (* default; shift level: %d)\n", p->shift_dpi + 1);
    for (int i = 0; i < p->n_buttons; i++) {
        rslap_describe_binding(p->buttons[i], desc, sizeof(desc));
        if (i % 3 == 0)
            fputs(i ? "\n    " : "    ", stdout);
        printf("G%d=%-18s", i + 1, desc);
    }
    putchar('\n');
    if (ob->info.gshift) {
        printf("    G-shift bindings:\n");
        for (int i = 0; i < p->n_buttons; i++) {
            rslap_describe_binding(p->gshift_buttons[i], desc, sizeof(desc));
            printf("      G%d=%s\n", i + 1, desc);
        }
    }
}

/* read+decode one user slot; never-written or invalid slots fall back to
 * the ROM profile (OpenGMice load_all semantics) */
static int load_slot(const struct rslap_onboard *ob, struct rslap_profile *p,
                     int slot, const struct rslap_profile *rom, int enabled)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    int rc = rslap_ob_read_sector(ob, slot, raw, sizeof(raw));

    if (rc == RSLAP_RC_OK && rslap_crc_ok(raw, (size_t)ob->info.sector_size)) {
        rc = rslap_profile_decode(p, raw, (size_t)ob->info.sector_size, slot,
                                  ob->info.fmt, ob->info.button_count);
        p->enabled = enabled;
    } else {
        rc = rslap_profile_decode(p, rom->raw, rom->raw_len, slot,
                                  ob->info.fmt, ob->info.button_count);
        p->enabled = enabled;
    }
    if (!p->name[0])
        snprintf(p->name, sizeof(p->name), "Profile %d", slot);
    return rc;
}

/* ------------------------------------------------------------- sector edit */

/* read the active profile sector into raw; dies when the active profile is
 * the read-only ROM one or when the read fails */
static void read_active_sector(const struct app *a, uint8_t *raw, int *sector_out)
{
    int sector = rslap_ob_get_current_sector(&a->ob);
    int rc;

    if (sector < 0)
        rc_exit(sector, "reading the active profile");
    if (sector >= RSLAP_ROM_BASE)
        die(EX_DEVICE,
            "the factory ROM profile is active; run 'activate N' to pick a "
            "writable slot first");
    rc = rslap_ob_read_sector(&a->ob, sector, raw, RSLAP_MAX_SECTOR);
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "reading the active profile");
    *sector_out = sector;
}

/* ---------------------------------------------------------------- commands */

static int cmd_list(const char *match)
{
    struct hidpp_candidate cands[64];
    struct app a;
    int n = hidpp_scan(cands, 64);
    int shown = 0;
    uint8_t seen_units[8][4];
    int nseen = 0;

    for (int i = 0; i < n; i++) {
        int rc = try_open(&a, &cands[i]);

        if (rc != 0)
            continue;
        if (match) {
            char key[256], m[64];

            snprintf(key, sizeof(key), "%s %04x", a.name, a.dev.pid);
            for (size_t x = 0; key[x]; x++)
                key[x] = (char)tolower((unsigned char)key[x]);
            snprintf(m, sizeof(m), "%s", match);
            for (size_t x = 0; m[x]; x++)
                m[x] = (char)tolower((unsigned char)m[x]);
            if (!strstr(key, m)) {
                hidpp_close(&a.dev);
                continue;
            }
        }
        /* one line per physical mouse: dedupe receiver-slot endpoints
         * against the device's own node by unit id */
        if (a.has_unit) {
            int dup = 0;

            for (int j = 0; j < nseen; j++)
                if (memcmp(seen_units[j], a.unit, 4) == 0)
                    dup = 1;
            if (dup) {
                hidpp_close(&a.dev);
                continue;
            }
            if (nseen < 8)
                memcpy(seen_units[nseen++], a.unit, 4);
        }
        printf("%s  [%s, 046d:%04x, %s]\n", a.name, a.cand.link, a.dev.pid, a.cand.path);
        hidpp_close(&a.dev);
        shown++;
    }
    if (!shown) {
        puts("no compatible device found");
        puts("  hint: run 'lsusb | grep 046d' — the mouse or its receiver must be present");
        puts("  hint: if it is present but access fails, install 42-g305x.rules (README)");
        return EX_NO_DEVICE;
    }
    return 0;
}

static int cmd_info(struct app *a)
{
    int pct, hz, x, y;
    uint8_t lod;
    const char *status;
    struct rslap_profile rom, p;
    struct rslap_dir_entry entries[8];
    int ndir, active;
    int rom_secs[8], nrom;

    printf("%s  [%s, 046d:%04x, %s]\n", a->name, a->cand.link, a->dev.pid, a->cand.path);
    if (a->has_fw)
        printf("firmware: %s", a->fw);
    if (a->has_unit) {
        char key[16];

        unit_hex(a->unit, key, sizeof(key));
        printf("  unit id: %s", key);
    }
    putchar('\n');
    if (hidpp_get_battery(&a->dev, &pct, &status) == RSLAP_RC_OK)
        printf("battery: %d%% %s\n", pct, status);
    if (hidpp_get_live_dpi(&a->dev, &x, &y, &lod) == RSLAP_RC_OK)
        printf("live DPI: %d x %d\n", x, y);
    if (hidpp_get_live_rate(&a->dev, &hz) == RSLAP_RC_OK)
        printf("live report rate: %d Hz\n", hz);
    {
        int mode = rslap_ob_get_mode(&a->ob);

        active = rslap_ob_get_current_sector(&a->ob);
        printf("memory mode: %s, active sector 0x%04X, profile format %d\n",
               mode == RSLAP_MODE_ONBOARD ? "onboard" : "host",
               active < 0 ? 0 : active, a->ob.info.fmt);
    }

    nrom = rslap_ob_rom_sectors(&a->ob, rom_secs, 8);
    if (nrom > 0) {
        uint8_t raw[RSLAP_MAX_SECTOR];
        int rc = rslap_ob_read_sector(&a->ob, rom_secs[0], raw, sizeof(raw));

        if (rc == RSLAP_RC_OK &&
            rslap_profile_decode(&rom, raw, (size_t)a->ob.info.sector_size,
                                 rom_secs[0], a->ob.info.fmt,
                                 a->ob.info.button_count) == RSLAP_RC_OK) {
            putchar('\n');
            print_profile(&a->ob, &rom, rom_secs[0] == active);
        }
    } else {
        memset(&rom, 0, sizeof(rom));
    }

    ndir = rslap_ob_read_directory(&a->ob, 0, entries, 8);
    for (int slot = 1; slot <= a->ob.info.profile_count; slot++) {
        int enabled = 0;

        for (int j = 0; j < ndir; j++)
            if (entries[j].sector == slot)
                enabled = entries[j].enabled;
        if (load_slot(&a->ob, &p, slot, &rom, enabled) != RSLAP_RC_OK)
            continue;
        putchar('\n');
        print_profile(&a->ob, &p, slot == active);
    }
    return 0;
}

static int cmd_active(struct app *a)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    struct rslap_profile p;
    int sector = rslap_ob_get_current_sector(&a->ob);
    int rc;

    if (sector < 0)
        rc_exit(sector, "reading the active profile");
    rc = rslap_ob_read_sector(&a->ob, sector, raw, sizeof(raw));
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "reading the active profile");
    if (!rslap_crc_ok(raw, (size_t)a->ob.info.sector_size))
        die(EX_DEVICE, "active profile sector 0x%04X has an invalid CRC", sector);
    rc = rslap_profile_decode(&p, raw, (size_t)a->ob.info.sector_size, sector,
                              a->ob.info.fmt, a->ob.info.button_count);
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "decoding the active profile");
    print_profile(&a->ob, &p, 1);
    return 0;
}

static int cmd_backup(struct app *a, const char *file)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    FILE *f = fopen(file, "w");
    char key[16];
    int saved = 0;

    if (!f)
        die(EX_USAGE, "cannot write %s: %s", file, strerror(errno));
    unit_hex(a->unit, key, sizeof(key));
    fprintf(f, "# ratslap-g305x on-board memory backup v1\n");
    fprintf(f, "device %s\n", a->name);
    fprintf(f, "vid_pid 046d:%04x\n", a->dev.pid);
    fprintf(f, "unit_id %s\n", a->has_unit ? key : "unknown");
    fprintf(f, "format %d\n", a->ob.info.fmt);
    fprintf(f, "sector_size %d\n", a->ob.info.sector_size);
    fprintf(f, "profile_count %d\n", a->ob.info.profile_count);
    for (int s = 0; s <= a->ob.info.profile_count; s++) {
        int rc = rslap_ob_read_sector(&a->ob, s, raw, sizeof(raw));

        if (rc != RSLAP_RC_OK)
            continue; /* never-written flash sector */
        fprintf(f, "sector 0x%04X ", s);
        for (int i = 0; i < a->ob.info.sector_size; i++)
            fprintf(f, "%02x", raw[i]);
        fputc('\n', f);
        saved++;
    }
    fclose(f);
    remember_backup(a, file);
    printf("saved %d sectors to %s\n", saved, file);
    printf("backup registered; modify commands now run without --yes\n");
    return 0;
}

static int cmd_restore(struct app *a, const char *file, int force)
{
    char line[8192];
    int fmt = -1, sector_size = -1, restored = 0, skipped = 0;
    char vid_pid[32] = "";
    int have_fmt = 0, have_size = 0, have_vid = 0;
    char want[32];
    FILE *f;

    gate_write(a, force);
    snprintf(want, sizeof(want), "046d:%04x", a->dev.pid);

    /* first pass: header fields, so a mismatched backup is refused before
     * anything is written */
    f = fopen(file, "r");
    if (!f)
        die(EX_USAGE, "cannot read %s: %s", file, strerror(errno));
    while (fgets(line, sizeof(line), f)) {
        char k[32];

        if (line[0] == '#' || sscanf(line, "%31s", k) != 1)
            continue;
        if (strcmp(k, "vid_pid") == 0) {
            sscanf(line, "%*s %31s", vid_pid);
            have_vid = 1;
        } else if (strcmp(k, "format") == 0) {
            sscanf(line, "%*s %d", &fmt);
            have_fmt = 1;
        } else if (strcmp(k, "sector_size") == 0) {
            sscanf(line, "%*s %d", &sector_size);
            have_size = 1;
        }
    }
    fclose(f);
    if (have_vid && strcmp(vid_pid, want) != 0)
        die(EX_DEVICE, "backup is from a different device (%s, this mouse is %s)",
            vid_pid, want);
    if (have_fmt && fmt != a->ob.info.fmt)
        die(EX_DEVICE, "backup has profile format %d, this mouse uses %d",
            fmt, a->ob.info.fmt);
    if (have_size && sector_size != a->ob.info.sector_size)
        die(EX_DEVICE, "backup has sector size %d, this mouse uses %d",
            sector_size, a->ob.info.sector_size);

    /* second pass: sector payloads */
    f = fopen(file, "r");
    if (!f)
        die(EX_USAGE, "cannot read %s: %s", file, strerror(errno));
    while (fgets(line, sizeof(line), f)) {
        char k[32];

        if (line[0] == '#' || sscanf(line, "%31s", k) != 1)
            continue;
        if (strcmp(k, "sector") != 0 || !have_size)
            continue;
        {
            unsigned usec;
            char hex[8192];
            uint8_t raw[RSLAP_MAX_SECTOR];
            int sector, bad = 0;

            if (sscanf(line, "%*s 0x%04X %8191s", &usec, hex) != 2)
                continue;
            sector = (int)usec;
            if ((size_t)strlen(hex) != (size_t)sector_size * 2)
                continue;
            for (int i = 0; i < sector_size; i++) {
                unsigned b;

                if (sscanf(hex + 2 * i, "%2x", &b) != 1) {
                    bad = 1;
                    break;
                }
                raw[i] = (uint8_t)b;
            }
            if (bad)
                continue;
            if (sector >= RSLAP_ROM_BASE) {
                printf("skipped sector 0x%04X (read-only ROM region)\n", sector);
                skipped++;
                continue;
            }
            if (!rslap_crc_ok(raw, (size_t)sector_size)) {
                printf("skipped sector 0x%04X (invalid CRC in backup)\n", sector);
                skipped++;
                continue;
            }
            int rc = rslap_ob_write_sector(&a->ob, sector, raw, (size_t)sector_size);

            if (rc != RSLAP_RC_OK) {
                printf("skipped sector 0x%04X (device refused or verify failed)\n", sector);
                skipped++;
                continue;
            }
            printf("restored sector 0x%04X\n", sector);
            restored++;
        }
    }
    fclose(f);
    printf("restored %d sector(s), skipped %d\n", restored, skipped);
    return 0;
}

static int cmd_button(struct app *a, int argc, char **argv, int force, int gshift)
{
    uint8_t raw[RSLAP_MAX_SECTOR], binding[4];
    const struct rslap_layout *l;
    char desc[64];
    int n, sector, off;

    if (argc != 2)
        die(EX_USAGE, "usage: %s N BINDING (see --listkeys)", gshift ? "gbutton" : "button");
    n = atoi(argv[0]);
    if (n < 1 || (size_t)n > RSLAP_MAX_BUTTONS || n > a->ob.info.button_count)
        die(EX_USAGE, "button number must be 1..%d", a->ob.info.button_count);
    if (gshift && !a->ob.info.gshift)
        die(EX_USAGE, "this mouse has no G-shift bindings");
    if (rslap_parse_binding(argv[1], binding) != RSLAP_RC_OK)
        die(EX_USAGE, "unknown binding '%s' — run with --listkeys to see them all", argv[1]);

    gate_write(a, force);
    read_active_sector(a, raw, &sector);
    l = rslap_layout_for(a->ob.info.fmt);
    off = (gshift ? l->gshift : l->buttons) + 4 * (n - 1);
    memcpy(raw + off, binding, 4);
    rslap_with_crc(raw, (size_t)a->ob.info.sector_size);
    int rc = rslap_ob_write_sector(&a->ob, sector, raw, (size_t)a->ob.info.sector_size);

    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "writing the button binding");
    rslap_describe_binding(binding, desc, sizeof(desc));
    printf("%sG%d = %s written to slot %d (verified)\n",
           gshift ? "G-shift " : "", n, desc, sector);
    return 0;
}

static int cmd_dpi(struct app *a, int argc, char **argv, int force)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    const struct rslap_layout *l;
    int level, x, y, sector, base;
    int dpi_vals[64];
    int n_dpi;

    if (argc < 2)
        die(EX_USAGE, "usage: dpi LEVEL X [Y]   or   dpi --live X [Y]");
    if (strcmp(argv[0], "--live") == 0) {
        int mode, lx, ly;
        uint8_t lod;
        int preview = 0;

        x = atoi(argv[1]);
        if (x <= 0)
            die(EX_USAGE, "DPI value must be positive");
        y = (argc >= 3) ? atoi(argv[2]) : x;
        /* the firmware refuses live setDpi in on-board mode (HID++ error
         * 0x05): temporarily switch to host mode, like OpenGMice */
        mode = rslap_ob_get_mode(&a->ob);
        if (mode == RSLAP_MODE_ONBOARD) {
            if (rslap_ob_set_mode(&a->ob, RSLAP_MODE_HOST) != RSLAP_RC_OK)
                die(EX_DEVICE, "cannot switch to host mode for the preview");
            preview = 1;
        }
        if (hidpp_get_live_dpi(&a->dev, &lx, &ly, &lod) != RSLAP_RC_OK)
            lod = 0;
        int rc = hidpp_set_live_dpi(&a->dev, x, y, lod);

        if (rc != RSLAP_RC_OK) {
            if (preview)
                rslap_ob_set_mode(&a->ob, RSLAP_MODE_ONBOARD);
            rc_exit(rc, "setting live DPI");
        }
        if (preview)
            rslap_ob_set_mode(&a->ob, RSLAP_MODE_ONBOARD);
        printf("live DPI now %d x %d (preview only, not saved; profile unchanged)\n", x, y);
        return 0;
    }

    level = atoi(argv[0]);
    x = atoi(argv[1]);
    y = (argc >= 3) ? atoi(argv[2]) : x;
    if (level < 1 || level > RSLAP_DPI_SLOTS)
        die(EX_USAGE, "DPI level must be 1..%d", RSLAP_DPI_SLOTS);
    if (x <= 0)
        die(EX_USAGE, "DPI value must be positive");

    n_dpi = hidpp_get_dpi_list(&a->dev, dpi_vals, 64);
    if (n_dpi > 0) {
        /* snap to the closest values the sensor actually supports */
        int best = 0;

        for (int i = 1; i < n_dpi; i++)
            if (abs(dpi_vals[i] - x) < abs(dpi_vals[best] - x))
                best = i;
        if (dpi_vals[best] != x) {
            printf("snapped %d to the nearest supported value %d\n", x, dpi_vals[best]);
            x = dpi_vals[best];
        }
        if (argc < 3) {
            y = x;
        } else {
            best = 0;
            for (int i = 1; i < n_dpi; i++)
                if (abs(dpi_vals[i] - y) < abs(dpi_vals[best] - y))
                    best = i;
            if (dpi_vals[best] != y) {
                printf("snapped Y %d to %d\n", y, dpi_vals[best]);
                y = dpi_vals[best];
            }
        }
    }

    gate_write(a, force);
    read_active_sector(a, raw, &sector);
    l = rslap_layout_for(a->ob.info.fmt);
    base = l->dpi + 2 + (level - 1) * l->stage_size;
    if (l->stage_size == 5) {
        int was_empty = rslap_le16(raw + base) == 0 ||
                        rslap_le16(raw + base) == 0xFFFF;

        rslap_put_le16(raw + base, (uint16_t)x);
        rslap_put_le16(raw + base + 2, (uint16_t)y);
        if (was_empty)
            raw[base + 4] = raw[l->dpi + 6]; /* new stage inherits stage 1 LOD */
    } else {
        rslap_put_le16(raw + base, (uint16_t)x);
    }
    rslap_with_crc(raw, (size_t)a->ob.info.sector_size);
    int rc = rslap_ob_write_sector(&a->ob, sector, raw, (size_t)a->ob.info.sector_size);

    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "writing the DPI level");
    printf("DPI level %d -> %d x %d written to slot %d (verified)\n",
           level, x, y, sector);
    return 0;
}

static int cmd_rate(struct app *a, int argc, char **argv, int force)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    const struct rslap_layout *l;
    int hz, idx, sector;
    int do_wireless = 1, do_wired = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wireless") == 0)
            do_wired = 0;
        else if (strcmp(argv[i], "--wired") == 0)
            do_wireless = 0;
        else
            die(EX_USAGE, "unknown option '%s' for rate", argv[i]);
    }
    if (argc < 1)
        die(EX_USAGE, "usage: rate HZ [--wireless|--wired]");
    hz = atoi(argv[0]);
    idx = rslap_rate_index(hz);
    if (idx < 0)
        die(EX_USAGE, "report rate must be one of 125, 250, 500, 1000");

    gate_write(a, force);
    read_active_sector(a, raw, &sector);
    l = rslap_layout_for(a->ob.info.fmt);
    if (l->rate_is_index) {
        if (do_wireless)
            raw[l->rate_wireless] = (uint8_t)idx;
        if (do_wired)
            raw[l->rate_wired] = (uint8_t)idx;
    } else {
        raw[l->rate_wireless] = (uint8_t)(1000 / hz); /* single shared byte */
    }
    rslap_with_crc(raw, (size_t)a->ob.info.sector_size);
    int rc = rslap_ob_write_sector(&a->ob, sector, raw, (size_t)a->ob.info.sector_size);

    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "writing the report rate");
    printf("report rate %d Hz written to slot %d (verified)\n", hz, sector);
    return 0;
}

static int cmd_activate(struct app *a, int argc, char **argv)
{
    int n, rc;

    if (argc != 1)
        die(EX_USAGE, "usage: activate N");
    n = atoi(argv[0]);
    if (n < 1 || n > a->ob.info.profile_count)
        die(EX_USAGE, "profile slot must be 1..%d", a->ob.info.profile_count);
    rc = rslap_ob_activate(&a->ob, n);
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "activating the profile");
    printf("active profile: slot %d\n", n);
    return 0;
}

static int cmd_reset(struct app *a, int argc, char **argv, int force)
{
    uint8_t romraw[RSLAP_MAX_SECTOR];
    int rom_secs[8], nrom;
    int slot, rc;

    if (argc > 1)
        die(EX_USAGE, "usage: reset [N]");
    slot = (argc == 1) ? atoi(argv[0]) : -1;
    if (argc == 1 && (slot < 1 || slot > a->ob.info.profile_count))
        die(EX_USAGE, "profile slot must be 1..%d", a->ob.info.profile_count);
    if (slot < 0) {
        int cur = rslap_ob_get_current_sector(&a->ob);

        if (cur < 0)
            rc_exit(cur, "reading the active profile");
        slot = (cur >= RSLAP_ROM_BASE) ? 1 : cur;
    }

    nrom = rslap_ob_rom_sectors(&a->ob, rom_secs, 8);
    if (nrom < 1)
        die(EX_DEVICE, "cannot read the factory profile from ROM");
    rc = rslap_ob_read_sector(&a->ob, rom_secs[0], romraw, sizeof(romraw));
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "reading the ROM profile");

    gate_write(a, force);
    rc = rslap_ob_write_sector(&a->ob, slot, romraw, (size_t)a->ob.info.sector_size);
    if (rc != RSLAP_RC_OK)
        rc_exit(rc, "writing the ROM copy");
    printf("slot %d reset to the factory profile (verified)\n", slot);
    return 0;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    struct app a;
    const char *match = NULL;
    int force = 0, i = 1;

    /* --yes may appear anywhere on the command line; strip it first */
    {
        int keep = 0;

        for (int j = 1; j < argc; j++) {
            if (strcmp(argv[j], "--yes") == 0) {
                force = 1;
                continue;
            }
            argv[++keep] = argv[j];
        }
        argc = keep + 1;
    }

    for (; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            printf("ratslap-g305x %s\n", RSLAP_VERSION);
            return 0;
        } else if (strcmp(arg, "--listkeys") == 0) {
            rslap_print_keylist(stdout);
            return 0;
        } else if (strcmp(arg, "--yes") == 0) {
            force = 1;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--device") == 0) {
            if (i + 1 >= argc)
                die(EX_USAGE, "-d needs a value (name or product id fragment)");
            match = argv[++i];
        } else {
            break; /* first positional: the command */
        }
    }

    const char *cmd = (i < argc) ? argv[i] : "";
    char **args = argv + i + 1;
    int nargs = argc - i - 1;

    if (strcmp(cmd, "list") == 0)
        return cmd_list(match);

    /* validate the command before touching any device, so usage errors
     * exit 3 even with no mouse attached */
    if (cmd[0] != '\0' && strcmp(cmd, "info") != 0 &&
        strcmp(cmd, "backup") != 0 && strcmp(cmd, "restore") != 0 &&
        strcmp(cmd, "button") != 0 && strcmp(cmd, "gbutton") != 0 &&
        strcmp(cmd, "dpi") != 0 && strcmp(cmd, "rate") != 0 &&
        strcmp(cmd, "activate") != 0 && strcmp(cmd, "reset") != 0) {
        fprintf(stderr, "unknown command '%s'\n\n", cmd);
        usage(stderr);
        return EX_USAGE;
    }

    open_app(&a, match);

    if (cmd[0] == '\0')
        return cmd_active(&a);
    if (strcmp(cmd, "info") == 0)
        return cmd_info(&a);
    if (strcmp(cmd, "backup") == 0) {
        if (nargs != 1)
            die(EX_USAGE, "usage: backup FILE");
        return cmd_backup(&a, args[0]);
    }
    if (strcmp(cmd, "restore") == 0) {
        if (nargs != 1)
            die(EX_USAGE, "usage: restore FILE");
        return cmd_restore(&a, args[0], force);
    }
    if (strcmp(cmd, "button") == 0)
        return cmd_button(&a, nargs, args, force, 0);
    if (strcmp(cmd, "gbutton") == 0)
        return cmd_button(&a, nargs, args, force, 1);
    if (strcmp(cmd, "dpi") == 0)
        return cmd_dpi(&a, nargs, args, force);
    if (strcmp(cmd, "rate") == 0)
        return cmd_rate(&a, nargs, args, force);
    if (strcmp(cmd, "activate") == 0)
        return cmd_activate(&a, nargs, args);
    if (strcmp(cmd, "reset") == 0)
        return cmd_reset(&a, nargs, args, force);

    fprintf(stderr, "unknown command '%s'\n\n", cmd); /* unreachable: validated above */
    usage(stderr);
    return EX_USAGE;
}

void die(int code, const char *fmt, ...)
{
    va_list ap;

    fputs("ratslap-g305x: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(code);
}
