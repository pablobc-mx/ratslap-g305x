/* Format-7 (v6 layout) and format-3 (v1 layout) profile sector tests:
 * decode against the golden sectors, encode round trip byte-for-byte,
 * and field-level edits that must not disturb any other byte. */
#include "onboard.h"
#include "golden.h"
#include <string.h>

static int fails = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                                    \
        }                                                               \
    } while (0)

static void check_v7_decode(const struct rslap_profile *p)
{
    CHECK(p->raw_len == GOLDEN_SECTOR_SIZE);
    CHECK(p->report_rate == 1000);
    CHECK(p->report_rate_wired == 1000);
    CHECK(p->default_dpi == 1);
    CHECK(p->shift_dpi == 0);
    CHECK(p->n_stages == 5);
    CHECK(p->stages[0].x == 400 && p->stages[0].y == 400 && p->stages[0].lod == 1);
    CHECK(p->stages[1].x == 800 && p->stages[1].y == 800 && p->stages[1].lod == 1);
    CHECK(p->stages[2].x == 1600 && p->stages[2].y == 1600 && p->stages[2].lod == 2);
    CHECK(p->stages[3].x == 3200 && p->stages[3].y == 3200 && p->stages[3].lod == 2);
    CHECK(p->stages[4].x == 6400 && p->stages[4].y == 6400 && p->stages[4].lod == 0);
    CHECK(p->n_buttons == 6);
    CHECK(p->buttons[0][0] == 0x80 && p->buttons[0][1] == 0x01 &&
          p->buttons[0][2] == 0x00 && p->buttons[0][3] == 0x01);
    CHECK(p->buttons[5][0] == 0x90 && p->buttons[5][1] == 0x07);
    CHECK(p->gshift_buttons[0][0] == 0xFF && p->gshift_buttons[0][3] == 0xFF);
    CHECK(strcmp(p->name, "Profile 1") == 0);
    CHECK(p->ps_timeout == 300);
    CHECK(p->po_timeout == 600);
}

int main(void)
{
    struct rslap_profile p;
    uint8_t out[RSLAP_MAX_SECTOR];
    int n;

    /* ---- v6 / format 7 golden decode ---- */
    memset(&p, 0, sizeof(p));
    CHECK(rslap_profile_decode(&p, GOLDEN_V7, GOLDEN_SECTOR_SIZE, 1, 7, 6) == RSLAP_RC_OK);
    check_v7_decode(&p);

    /* unknown/vendor bytes and lighting are carried in raw untouched */
    CHECK(p.raw[0x20] == 0xAA && p.raw[0x21] == 0xBB);
    CHECK(p.raw[0x22] == 0x00);
    CHECK(p.raw[0xD0] == 0x00 && p.raw[0xFB] == 0x00);

    /* ---- v6 encode round trip: must reproduce the golden bytes ---- */
    n = rslap_profile_encode(&p, out, sizeof(out));
    CHECK(n == GOLDEN_SECTOR_SIZE);
    CHECK(memcmp(out, GOLDEN_V7, GOLDEN_SECTOR_SIZE) == 0);
    CHECK(rslap_crc_ok(out, GOLDEN_SECTOR_SIZE));

    /* ---- field edit: one button -> only those 4 bytes + CRC move ---- */
    {
        uint8_t binding[4] = {0x80, 0x03, 0x00, 0xB5}; /* media: Next Track */
        memcpy(p.buttons[3], binding, 4);
        n = rslap_profile_encode(&p, out, sizeof(out));
        CHECK(n == GOLDEN_SECTOR_SIZE);
        CHECK(memcmp(out + 0x30, GOLDEN_V7 + 0x30, 16) != 0); /* region changed */
        CHECK(memcmp(out + 0x3C, binding, 4) == 0);           /* G4 slot */
        CHECK(memcmp(out, GOLDEN_V7, 0x30) == 0);             /* all before buttons */
        CHECK(memcmp(out + 0x44, GOLDEN_V7 + 0x44, 0xA0 - 0x44) == 0);
        CHECK(rslap_crc_ok(out, GOLDEN_SECTOR_SIZE));
    }

    /* ---- field edit: DPI stage 3 ---- */
    {
        struct rslap_profile q;
        memset(&q, 0, sizeof(q));
        CHECK(rslap_profile_decode(&q, GOLDEN_V7, GOLDEN_SECTOR_SIZE, 1, 7, 6) == RSLAP_RC_OK);
        q.stages[2].x = 2400;
        q.stages[2].y = 2400;
        n = rslap_profile_encode(&q, out, sizeof(out));
        CHECK(n == GOLDEN_SECTOR_SIZE);
        /* stage 3 record at 0x04 + 2*5 = 0x0E */
        CHECK(rslap_le16(out + 0x0E) == 2400);
        CHECK(rslap_le16(out + 0x10) == 2400);
        CHECK(out[0x12] == 2); /* LOD preserved */
        CHECK(memcmp(out, GOLDEN_V7, 0x0E) == 0);
        CHECK(memcmp(out + 0x13, GOLDEN_V7 + 0x13, 0x30 - 0x13) == 0);
        CHECK(rslap_crc_ok(out, GOLDEN_SECTOR_SIZE));
    }

    /* ---- ROM golden decodes too ---- */
    {
        struct rslap_profile r;
        memset(&r, 0, sizeof(r));
        CHECK(rslap_profile_decode(&r, GOLDEN_V7_ROM, GOLDEN_SECTOR_SIZE, 0x101, 7, 6) == RSLAP_RC_OK);
        CHECK(r.n_stages == 3);
        CHECK(strcmp(r.name, "Default") == 0);
        CHECK(r.buttons[5][1] == 0x05); /* DPI Cycle */
        CHECK(rslap_profile_encode(&r, out, sizeof(out)) == GOLDEN_SECTOR_SIZE);
        /* 3 stages: used slots preserved; the two unused slots are
         * zero-filled (OpenGMice-verified behavior), CRC updated */
        CHECK(memcmp(out, GOLDEN_V7_ROM, 0x13) == 0);
        CHECK(memcmp(out + 0x13, GOLDEN_V7_ROM + 0x13, 10) != 0); /* zeros vs FF */
        for (int i = 0x13; i < 0x1D; i++)
            CHECK(out[i] == 0x00);
        CHECK(memcmp(out + 0x1D, GOLDEN_V7_ROM + 0x1D, 0xE1) == 0);
        CHECK(rslap_crc_ok(out, GOLDEN_SECTOR_SIZE));
    }

    /* ---- directory decode ---- */
    {
        struct rslap_dir_entry entries[8];
        /* parse the golden directory bytes the way the code will */
        const uint8_t *d = GOLDEN_DIR;
        int count = 0;
        for (int off = 0; off + 3 < GOLDEN_SECTOR_SIZE; off += 4) {
            int sec = rslap_be16(d + off);
            if (sec == 0xFFFF || sec == 0x0000)
                break;
            entries[count].sector = sec;
            entries[count].enabled = (d[off + 2] == 1);
            count++;
        }
        CHECK(count == 2);
        CHECK(entries[0].sector == 1 && entries[0].enabled);
        CHECK(entries[1].sector == 2 && entries[1].enabled);
        CHECK(rslap_crc_ok(GOLDEN_DIR, GOLDEN_SECTOR_SIZE));
    }

    /* ---- v1 (format 3, original G305) golden decode + round trip ---- */
    {
        struct rslap_profile v;
        memset(&v, 0, sizeof(v));
        CHECK(rslap_profile_decode(&v, GOLDEN_V1, GOLDEN_SECTOR_SIZE, 1, 3, 6) == RSLAP_RC_OK);
        CHECK(v.report_rate == 1000);
        CHECK(v.n_stages == 5);
        CHECK(v.stages[0].x == 400 && v.stages[0].y == 400); /* v1: y mirrors x */
        CHECK(v.stages[4].x == 6400);
        CHECK(v.default_dpi == 1);
        CHECK(strcmp(v.name, "Profile 1") == 0);
        CHECK(v.ps_timeout == 300 && v.po_timeout == 600);
        CHECK(rslap_profile_encode(&v, out, sizeof(out)) == GOLDEN_SECTOR_SIZE);
        CHECK(memcmp(out, GOLDEN_V1, GOLDEN_SECTOR_SIZE) == 0);
        /* v1 buttons live at 0x20, gshift at 0x60 */
        CHECK(v.buttons[0][1] == 0x01 && v.raw[0x20 + 4] == 0x80);
        CHECK(v.gshift_buttons[0][0] == 0xFF);
    }

    /* ---- layouts and rate mapping ---- */
    {
        const struct rslap_layout *l6 = rslap_layout_for(7);
        const struct rslap_layout *l1 = rslap_layout_for(3);
        CHECK(l6 && l6->rate_wireless == 0x00 && l6->rate_wired == 0x01);
        CHECK(l6->dpi == 0x02 && l6->stage_size == 5);
        CHECK(l6->ps_timeout == 0x2C && l6->po_timeout == 0x2E);
        CHECK(l6->buttons == 0x30 && l6->gshift == 0x70 && l6->name == 0xA0);
        CHECK(l6->rate_is_index);
        CHECK(l1 && l1->rate_wireless == 0x00 && l1->rate_wired == 0x00);
        CHECK(l1->dpi == 0x01 && l1->stage_size == 2);
        CHECK(l1->buttons == 0x20 && l1->gshift == 0x60);
        CHECK(!l1->rate_is_index);

        CHECK(rslap_rate_index(125) == 0);
        CHECK(rslap_rate_index(1000) == 3);
        CHECK(rslap_rate_index(4000) == 5);
        CHECK(rslap_rate_index(123) == -1);
        CHECK(rslap_rate_from_index(3) == 1000);
        CHECK(rslap_rate_from_index(0) == 125);
        CHECK(rslap_rate_from_index(99) == 1000); /* out of range -> default */
    }

    /* ---- decode guards ---- */
    CHECK(rslap_profile_decode(&p, GOLDEN_V7, 16, 1, 7, 6) == RSLAP_RC_MALFORMED);
    {
        uint8_t small[4];
        struct rslap_profile q;
        memset(&q, 0, sizeof(q));
        memset(small, 0xFF, sizeof(small));
        /* encode into a too-small buffer must be refused */
        CHECK(rslap_profile_encode(&p, small, sizeof(small)) == RSLAP_RC_REFUSED);
    }

    if (fails) {
        fprintf(stderr, "test_profile: %d check(s) failed\n", fails);
        return 1;
    }
    puts("test_profile: OK");
    return 0;
}
