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

int rslap_ob_init(struct rslap_onboard *ob, struct hidpp_device *dev)
{
    (void)ob; (void)dev;
    return RSLAP_RC_UNSUPPORTED;
}

int rslap_ob_read_sector(const struct rslap_onboard *ob, int sector, uint8_t *out, size_t outlen)
{
    (void)ob; (void)sector; (void)out; (void)outlen;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_write_sector(const struct rslap_onboard *ob, int sector, const uint8_t *data, size_t len)
{
    (void)ob; (void)sector; (void)data; (void)len;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_get_mode(const struct rslap_onboard *ob)
{
    (void)ob;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_set_mode(const struct rslap_onboard *ob, int mode)
{
    (void)ob; (void)mode;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_get_current_sector(const struct rslap_onboard *ob)
{
    (void)ob;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_set_current_sector(const struct rslap_onboard *ob, int sector)
{
    (void)ob; (void)sector;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_read_directory(const struct rslap_onboard *ob, int rom,
                            struct rslap_dir_entry *out, int max)
{
    (void)ob; (void)rom; (void)out; (void)max;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_write_directory(const struct rslap_onboard *ob,
                             const struct rslap_dir_entry *entries, int n)
{
    (void)ob; (void)entries; (void)n;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_rom_sectors(const struct rslap_onboard *ob, int *out, int max)
{
    (void)ob; (void)out; (void)max;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_activate(struct rslap_onboard *ob, int sector)
{
    (void)ob; (void)sector;
    return RSLAP_RC_MALFORMED;
}

int rslap_ob_get_dpi_index(const struct rslap_onboard *ob)
{
    (void)ob;
    return RSLAP_RC_MALFORMED;
}
