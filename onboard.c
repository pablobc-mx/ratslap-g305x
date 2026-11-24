/* stub — profile machinery implemented after its tests exist.
 * The CRC trio is real (test_crc.c drove its implementation). */
#include "onboard.h"
#include <string.h>

const int rslap_report_rates[RSLAP_N_RATES] = {125, 250, 500, 1000, 2000, 4000, 8000};

int rslap_crc_ccitt(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u)
                crc = (crc << 1) ^ 0x1021u;
            else
                crc <<= 1;
            crc &= 0xFFFFu;
        }
    }
    return (int)(crc & 0xFFFFu);
}

void rslap_with_crc(uint8_t *data, size_t len)
{
    uint16_t crc;

    if (len < 2)
        return;
    crc = (uint16_t)rslap_crc_ccitt(data, len - 2);
    data[len - 2] = (uint8_t)(crc >> 8);
    data[len - 1] = (uint8_t)(crc & 0xFF);
}

int rslap_crc_ok(const uint8_t *data, size_t len)
{
    if (len < 2 || data == NULL)
        return 0;
    return rslap_crc_ccitt(data, len - 2) == rslap_be16(data + len - 2);
}

/* Layout constants (verified against a format-7 G305 X by OpenGMice). */
static const struct rslap_layout layout_v6 = {
    .rate_wireless = 0x00, .rate_wired = 0x01,
    .dpi = 0x02, .stage_size = 5,
    .ps_timeout = 0x2C, .po_timeout = 0x2E,
    .buttons = 0x30, .gshift = 0x70, .name = 0xA0,
    .rate_is_index = 1,
};

static const struct rslap_layout layout_v1 = {
    .rate_wireless = 0x00, .rate_wired = 0x00,
    .dpi = 0x01, .stage_size = 2,
    .ps_timeout = 0x1C, .po_timeout = 0x1E,
    .buttons = 0x20, .gshift = 0x60, .name = 0xA0,
    .rate_is_index = 0,
};

const struct rslap_layout *rslap_layout_for(int fmt)
{
    return fmt >= 6 ? &layout_v6 : &layout_v1;
}

int rslap_rate_index(int hz)
{
    for (int i = 0; i < RSLAP_N_RATES; i++)
        if (rslap_report_rates[i] == hz)
            return i;
    return -1;
}

int rslap_rate_from_index(int idx)
{
    if (idx < 0 || idx >= RSLAP_N_RATES)
        return 1000;
    return rslap_report_rates[idx];
}

static int rate_from_byte(uint8_t b, const struct rslap_layout *l)
{
    if (l->rate_is_index)
        return rslap_rate_from_index(b);
    return (b > 0 && b != 0xFF) ? 1000 / b : 1000;
}

static uint8_t rate_to_byte(int hz, const struct rslap_layout *l)
{
    if (l->rate_is_index) {
        int i = rslap_rate_index(hz);
        return (uint8_t)(i < 0 ? 3 : i); /* unknown -> 1000 Hz */
    }
    return (uint8_t)(hz > 0 ? 1000 / hz : 1);
}

int rslap_profile_decode(struct rslap_profile *p, const uint8_t *raw,
                         size_t len, int sector, int fmt, int n_buttons)
{
    const struct rslap_layout *l = rslap_layout_for(fmt);

    if (raw == NULL || p == NULL || l == NULL)
        return RSLAP_RC_MALFORMED;
    if (len < (size_t)l->name + 48)
        return RSLAP_RC_MALFORMED;
    if (n_buttons < 0 || n_buttons > RSLAP_BUTTON_SLOTS)
        return RSLAP_RC_MALFORMED;

    memset(p, 0, sizeof(*p));
    p->sector = sector;
    p->fmt = fmt;
    p->raw_len = len < RSLAP_MAX_SECTOR ? len : RSLAP_MAX_SECTOR;
    memcpy(p->raw, raw, p->raw_len);

    p->report_rate = rate_from_byte(raw[l->rate_wireless], l);
    p->report_rate_wired = rate_from_byte(raw[l->rate_wired], l);
    p->default_dpi = raw[l->dpi] == 0xFF ? 0 : raw[l->dpi];
    p->shift_dpi = raw[l->dpi + 1] == 0xFF ? 0 : raw[l->dpi + 1];

    for (int i = 0; i < RSLAP_DPI_SLOTS; i++) {
        int base = l->dpi + 2 + i * l->stage_size;
        int x = rslap_le16(raw + base);
        int y, lod;

        if (x == 0 || x == 0xFFFF)
            break;
        if (l->stage_size == 5) {
            y = rslap_le16(raw + base + 2);
            lod = raw[base + 4];
        } else {
            y = x;
            lod = 0;
        }
        p->stages[p->n_stages].x = x;
        p->stages[p->n_stages].y = (y == 0 || y == 0xFFFF) ? x : y;
        p->stages[p->n_stages].lod = lod;
        p->n_stages++;
    }

    p->n_buttons = n_buttons;
    for (int i = 0; i < n_buttons; i++) {
        memcpy(p->buttons[i], raw + l->buttons + 4 * i, 4);
        memcpy(p->gshift_buttons[i], raw + l->gshift + 4 * i, 4);
    }

    /* name: 48 bytes UTF-16LE; 0xFF-filled means "unset" */
    {
        int unset = 1;
        for (int i = 0; i < 48; i++)
            if (raw[l->name + i] != 0xFF) {
                unset = 0;
                break;
            }
        if (!unset) {
            size_t o = 0;
            for (int i = 0; i + 1 < 48 && o + 1 < sizeof(p->name); i += 2) {
                uint16_t u = rslap_le16(raw + l->name + i);
                if (u == 0)
                    break;
                /* keep it simple: ASCII range passes through, the rest
                 * becomes '?', matching what G HUB ever stores */
                p->name[o++] = (u < 0x80) ? (char)u : '?';
            }
            p->name[o] = '\0';
        }
    }

    p->ps_timeout = rslap_le16(raw + l->ps_timeout);
    p->po_timeout = rslap_le16(raw + l->po_timeout);
    return RSLAP_RC_OK;
}

int rslap_profile_encode(const struct rslap_profile *p, uint8_t *out, size_t outlen)
{
    const struct rslap_layout *l = rslap_layout_for(p->fmt);
    size_t len = p->raw_len;
    uint8_t d[RSLAP_MAX_SECTOR];

    if (l == NULL || len < (size_t)l->name + 48 || len < 2)
        return RSLAP_RC_MALFORMED;
    if (outlen < len)
        return RSLAP_RC_REFUSED;
    if (p->n_stages < 1 || p->n_stages > RSLAP_DPI_SLOTS)
        return RSLAP_RC_REFUSED;

    memcpy(d, p->raw, len);

    d[l->rate_wireless] = rate_to_byte(p->report_rate, l);
    if (l->rate_wired != l->rate_wireless)
        d[l->rate_wired] = rate_to_byte(p->report_rate_wired, l);

    d[l->dpi] = (uint8_t)(p->default_dpi <= p->n_stages - 1 ? p->default_dpi : p->n_stages - 1);
    d[l->dpi + 1] = (uint8_t)(p->shift_dpi <= p->n_stages - 1 ? p->shift_dpi : p->n_stages - 1);

    {
        int first_lod = d[l->dpi + 6]; /* LOD of stage 1, copied to new stages */
        for (int i = 0; i < RSLAP_DPI_SLOTS; i++) {
            int base = l->dpi + 2 + i * l->stage_size;
            if (i >= p->n_stages) {
                memset(d + base, 0, (size_t)l->stage_size);
                continue;
            }
            rslap_put_le16(d + base, (uint16_t)p->stages[i].x);
            if (l->stage_size == 5) {
                rslap_put_le16(d + base + 2, (uint16_t)p->stages[i].y);
                /* LOD is never edited: keep the stored byte; brand-new
                 * stages inherit stage 1's LOD */
                d[base + 4] = rslap_le16(p->raw + base) == 0 ||
                              rslap_le16(p->raw + base) == 0xFFFF
                              ? (uint8_t)first_lod : p->raw[base + 4];
            }
        }
    }

    for (int i = 0; i < p->n_buttons && i < RSLAP_BUTTON_SLOTS; i++)
        memcpy(d + l->buttons + 4 * i, p->buttons[i], 4);

    if (p->name[0] != '\0') {
        memset(d + l->name, 0, 48);
        for (size_t i = 0; i + 1 < 48 && p->name[i] != '\0' && i < 23; i++)
            rslap_put_le16(d + l->name + 2 * i, (uint16_t)(unsigned char)p->name[i]);
    }

    rslap_put_le16(d + l->ps_timeout, (uint16_t)p->ps_timeout);
    rslap_put_le16(d + l->po_timeout, (uint16_t)p->po_timeout);

    rslap_with_crc(d, len);
    memcpy(out, d, len);
    return (int)len;
}

/* --------------------------------------------------------- 0x8100 ops */

int rslap_ob_init(struct rslap_onboard *ob, struct hidpp_device *dev)
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc;

    memset(ob, 0, sizeof(*ob));
    ob->dev = dev;
    rc = hidpp_request(dev, FEAT_ONBOARD, 0, NULL, 0, out);
    if (rc != RSLAP_RC_OK)
        return rc;
    ob->info.memory_model = out[0];
    ob->info.fmt = out[1];
    ob->info.macro_fmt = out[2];
    ob->info.profile_count = out[3];
    ob->info.profile_count_oob = out[4];
    ob->info.button_count = out[5];
    ob->info.sector_count = out[6];
    ob->info.sector_size = (out[7] << 8) | out[8];
    ob->info.gshift = (out[9] & 0x03) == 0x02;

    if (ob->info.memory_model != 1)
        return RSLAP_RC_REFUSED; /* unsupported onboard memory model */
    if (ob->info.sector_size < 2 || ob->info.sector_size > RSLAP_MAX_SECTOR)
        return RSLAP_RC_MALFORMED;
    if (ob->info.button_count < 1 || ob->info.button_count > RSLAP_MAX_BUTTONS)
        return RSLAP_RC_MALFORMED;
    return RSLAP_RC_OK;
}

int rslap_ob_read_sector(const struct rslap_onboard *ob, int sector,
                         uint8_t *out, size_t outlen)
{
    int size = ob->info.sector_size;
    int off = 0;

    if (sector < 0 || outlen < (size_t)size)
        return RSLAP_RC_REFUSED;

    while (off < size - 15) {
        uint8_t params[4] = {(uint8_t)(sector >> 8), (uint8_t)sector,
                             (uint8_t)(off >> 8), (uint8_t)off};
        uint8_t outp[HIDPP_MAX_PARAMS];
        int rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_READ, params, 4, outp);

        if (rc != RSLAP_RC_OK)
            return rc;
        memcpy(out + off, outp, 16);
        off += 16;
    }
    if (off < size) {
        /* last partial chunk: read 16 bytes ending exactly at sector end */
        uint8_t params[4] = {(uint8_t)(sector >> 8), (uint8_t)sector,
                             (uint8_t)((size - 16) >> 8), (uint8_t)(size - 16)};
        uint8_t outp[HIDPP_MAX_PARAMS];
        int rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_READ, params, 4, outp);
        int tail = size - off;

        if (rc != RSLAP_RC_OK)
            return rc;
        memcpy(out + off, outp + (16 - tail), (size_t)tail);
    }
    return RSLAP_RC_OK;
}

int rslap_ob_write_sector(const struct rslap_onboard *ob, int sector,
                          const uint8_t *data, size_t len)
{
    int size = ob->info.sector_size;
    uint8_t back[RSLAP_MAX_SECTOR];
    int rc;

    if (sector < 0 || sector >= RSLAP_ROM_BASE || sector >= ob->info.sector_count)
        return RSLAP_RC_REFUSED; /* read-only / out of range */
    if (len != (size_t)size)
        return RSLAP_RC_REFUSED;
    if (!rslap_crc_ok(data, len))
        return RSLAP_RC_REFUSED; /* never write a sector with a bad CRC */

    {
        uint8_t params[6] = {(uint8_t)(sector >> 8), (uint8_t)sector,
                             0, 0, (uint8_t)(size >> 8), (uint8_t)size};

        rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_WRITE_START, params, 6, NULL);
        if (rc != RSLAP_RC_OK)
            return rc;
    }
    for (int off = 0; off < size; off += 16) {
        uint8_t chunk[16];

        memcpy(chunk, data + off, 16);
        rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_WRITE_DATA, chunk, 16, NULL);
        if (rc != RSLAP_RC_OK)
            return rc;
    }
    rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_WRITE_END, NULL, 0, NULL);
    if (rc != RSLAP_RC_OK)
        return rc;

    /* read back and verify: never leave a mismatched sector behind */
    rc = rslap_ob_read_sector(ob, sector, back, sizeof(back));
    if (rc != RSLAP_RC_OK)
        return rc;
    if (memcmp(back, data, len) != 0)
        return RSLAP_RC_MALFORMED;
    return RSLAP_RC_OK;
}

int rslap_ob_get_mode(const struct rslap_onboard *ob)
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_GET_MODE, NULL, 0, out);

    return rc == RSLAP_RC_OK ? out[0] : rc;
}

int rslap_ob_set_mode(const struct rslap_onboard *ob, int mode)
{
    uint8_t params[1] = {(uint8_t)mode};

    return hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_SET_MODE, params, 1, NULL);
}

int rslap_ob_get_current_sector(const struct rslap_onboard *ob)
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_GET_SECTOR, NULL, 0, out);

    return rc == RSLAP_RC_OK ? (out[0] << 8) | out[1] : rc;
}

int rslap_ob_set_current_sector(const struct rslap_onboard *ob, int sector)
{
    uint8_t params[2] = {(uint8_t)(sector >> 8), (uint8_t)sector};

    return hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_SET_SECTOR, params, 2, NULL);
}

int rslap_ob_read_directory(const struct rslap_onboard *ob, int rom,
                            struct rslap_dir_entry *out, int max)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    int rc, n = 0;

    rc = rslap_ob_read_sector(ob, rom ? RSLAP_ROM_BASE : 0, raw, sizeof(raw));
    if (rc != RSLAP_RC_OK)
        return 0; /* e.g. never-written flash: report no entries */
    for (int off = 0; off + 3 < ob->info.sector_size && n < max; off += 4) {
        int sec = rslap_be16(raw + off);

        if (sec == 0xFFFF || sec == 0x0000)
            break;
        out[n].sector = sec;
        out[n].enabled = raw[off + 2] == 1;
        n++;
    }
    return n;
}

int rslap_ob_write_directory(const struct rslap_onboard *ob,
                            const struct rslap_dir_entry *entries, int n)
{
    uint8_t raw[RSLAP_MAX_SECTOR];
    int size = ob->info.sector_size;

    if (size > RSLAP_MAX_SECTOR || n * 4 + 2 > size)
        return RSLAP_RC_REFUSED;
    memset(raw, 0xFF, (size_t)size);
    for (int i = 0; i < n; i++) {
        raw[4 * i] = (uint8_t)(entries[i].sector >> 8);
        raw[4 * i + 1] = (uint8_t)entries[i].sector;
        raw[4 * i + 2] = (uint8_t)(entries[i].enabled ? 1 : 0);
    }
    rslap_with_crc(raw, (size_t)size);
    return rslap_ob_write_sector(ob, 0, raw, (size_t)size);
}

int rslap_ob_rom_sectors(const struct rslap_onboard *ob, int *out, int max)
{
    struct rslap_dir_entry entries[8];
    int n = rslap_ob_read_directory(ob, 1, entries, 8);

    if (n <= 0) {
        if (max < 1)
            return 0;
        out[0] = RSLAP_ROM_BASE + 1; /* documented fallback */
        return 1;
    }
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        out[i] = entries[i].sector;
    return n;
}

int rslap_ob_activate(struct rslap_onboard *ob, int sector)
{
    int cur = rslap_ob_get_current_sector(ob);
    struct rslap_dir_entry entries[8];
    int rom_secs[8];
    int nrom, ndir;

    if (cur < 0 && cur != sector)
        return cur; /* transport error other than "already there" */

    if (cur == sector) {
        /* re-selecting may not re-apply; hop to another profile first */
        nrom = rslap_ob_rom_sectors(ob, rom_secs, 8);
        for (int i = 0; i < nrom; i++) {
            if (rom_secs[i] != sector)
                rslap_ob_set_current_sector(ob, rom_secs[i]); /* best effort */
        }
        ndir = rslap_ob_read_directory(ob, 0, entries, 8);
        for (int i = 0; i < ndir; i++) {
            if (entries[i].enabled && entries[i].sector != sector)
                rslap_ob_set_current_sector(ob, entries[i].sector); /* best effort */
        }
    }
    return rslap_ob_set_current_sector(ob, sector);
}

int rslap_ob_get_dpi_index(const struct rslap_onboard *ob)
{
    uint8_t out[HIDPP_MAX_PARAMS];
    int rc = hidpp_request(ob->dev, FEAT_ONBOARD, OB_FN_GET_DPI_IDX, NULL, 0, out);

    return rc == RSLAP_RC_OK ? out[0] : rc;
}
