/* stub — profile machinery implemented after its tests exist.
 * The CRC trio is real (test_crc.c drove its implementation). */
#include "onboard.h"

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

const struct rslap_layout *rslap_layout_for(int fmt)
{
    (void)fmt;
    return NULL;
}

int rslap_rate_index(int hz)
{
    (void)hz;
    return -1;
}

int rslap_rate_from_index(int idx)
{
    (void)idx;
    return 1000;
}

int rslap_profile_decode(struct rslap_profile *p, const uint8_t *raw, size_t len,
                         int sector, int fmt, int n_buttons)
{
    (void)p; (void)raw; (void)len; (void)sector; (void)fmt; (void)n_buttons;
    return RSLAP_RC_MALFORMED;
}

int rslap_profile_encode(const struct rslap_profile *p, uint8_t *out, size_t outlen)
{
    (void)p; (void)out; (void)outlen;
    return RSLAP_RC_MALFORMED;
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
