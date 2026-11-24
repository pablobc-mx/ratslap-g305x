/* HID++ 2.0 frame build/parse tests.
 *
 * Checks the exact 20-byte long-report layout used by OpenGMice /
 * libratbag: [0x11, devnum, feature index, (fn << 4) | SW_ID, params...,
 * zero padding], plus reply classification (data reply, HID++ 2.0 error
 * on feature 0xFF, HID++ 1.0 error on 0x8F, junk rejection). */
#include "hidpp.h"
#include <string.h>

static int fails = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                                    \
        }                                                               \
    } while (0)

int main(void)
{
    uint8_t buf[HIDPP_LONG_LEN];
    uint8_t params[HIDPP_MAX_PARAMS];
    uint8_t devnum, idx, fnsw;
    enum hidpp_reply_kind kind;

    /* ---- request build ---- */
    {
        const uint8_t ping_params[3] = {0x00, 0x00, 0x5A};
        int n = hidpp_build_request(buf, sizeof(buf), 0xFF, 0x00, 0x01,
                                    ping_params, 3);
        CHECK(n == HIDPP_LONG_LEN);
        CHECK(buf[0] == 0x11);
        CHECK(buf[1] == 0xFF);
        CHECK(buf[2] == 0x00);
        CHECK(buf[3] == 0x1B); /* (fn 1 << 4) | SW_ID 0x0B */
        CHECK(buf[4] == 0x00 && buf[5] == 0x00 && buf[6] == 0x5A);
        CHECK(buf[7] == 0 && buf[19] == 0); /* zero padded */
    }
    {
        /* feature lookup: root fn 0, params = feature id 0x8100 */
        const uint8_t p[2] = {0x81, 0x00};
        int n = hidpp_build_request(buf, sizeof(buf), 0xFF, 0x00, 0x00, p, 2);
        CHECK(n == HIDPP_LONG_LEN);
        CHECK(buf[3] == 0x0B);
        CHECK(buf[4] == 0x81 && buf[5] == 0x00);
    }
    /* error paths */
    CHECK(hidpp_build_request(buf, sizeof(buf), 0xFF, 0, 0, NULL, 17) == -1);
    CHECK(hidpp_build_request(buf, 19, 0xFF, 0, 0, NULL, 0) == -1);

    /* ---- reply classify: data reply (long) ---- */
    {
        uint8_t reply[HIDPP_LONG_LEN] = {0x11, 0xFF, 0x06, 0x0B};
        for (int i = 0; i < 16; i++)
            reply[4 + i] = (uint8_t)(0xA0 + i);
        kind = hidpp_classify(reply, sizeof(reply), &devnum, &idx, &fnsw, params);
        CHECK(kind == HIDPP_REPLY_DATA);
        CHECK(devnum == 0xFF);
        CHECK(idx == 0x06);
        CHECK(fnsw == 0x0B);
        CHECK(params[0] == 0xA0 && params[15] == 0xAF);
    }
    /* short report reply: params zero padded beyond the report */
    {
        uint8_t reply[7] = {0x10, 0x03, 0x01, 0x1B, 0x02, 0x00, 0x5A};
        kind = hidpp_classify(reply, sizeof(reply), &devnum, &idx, &fnsw, params);
        CHECK(kind == HIDPP_REPLY_DATA);
        CHECK(devnum == 0x03 && idx == 0x01 && fnsw == 0x1B);
        CHECK(params[0] == 0x02 && params[1] == 0x00 && params[2] == 0x5A);
        CHECK(params[3] == 0 && params[15] == 0);
    }
    /* HID++ 2.0 error: [rep, dev, 0xFF, idx, fnsw, code, ...] */
    {
        uint8_t reply[HIDPP_LONG_LEN] = {0x11, 0xFF, 0xFF, 0x06, 0x0B, 0x05};
        kind = hidpp_classify(reply, sizeof(reply), &devnum, &idx, &fnsw, params);
        CHECK(kind == HIDPP_REPLY_ERR2);
        CHECK(idx == 0x06);
        CHECK(params[0] == 0x05); /* error code */
    }
    /* HID++ 1.0 error (empty receiver slot): [rep, dev, 0x8F, idx, fnsw, code] */
    {
        uint8_t reply[HIDPP_LONG_LEN] = {0x11, 0x02, 0x8F, 0x00, 0x1B, 0x01};
        kind = hidpp_classify(reply, sizeof(reply), &devnum, &idx, &fnsw, params);
        CHECK(kind == HIDPP_REPLY_ERR1);
        CHECK(devnum == 0x02);
        CHECK(idx == 0x00 && fnsw == 0x1B);
        CHECK(params[0] == 0x01);
    }
    /* junk: not for us */
    {
        uint8_t mouse_report[8] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        CHECK(hidpp_classify(mouse_report, sizeof(mouse_report), NULL, NULL, NULL, NULL) == HIDPP_REPLY_IGNORE);
        CHECK(hidpp_classify(mouse_report, 6, NULL, NULL, NULL, NULL) == HIDPP_REPLY_IGNORE); /* too short */
        uint8_t badrep[10] = {0x20, 0xFF, 0x06, 0x0B, 0, 0, 0, 0, 0, 0};
        CHECK(hidpp_classify(badrep, sizeof(badrep), NULL, NULL, NULL, NULL) == HIDPP_REPLY_IGNORE);
    }
    /* round trip: build a request, then the matching data reply for it */
    {
        const uint8_t req_params[4] = {0x00, 0x01, 0x00, 0x00};
        uint8_t reply[HIDPP_LONG_LEN];
        uint8_t rdev = 0x02, ridx = 0x05, rfn = 0x03;

        CHECK(hidpp_build_request(buf, sizeof(buf), rdev, ridx, rfn, req_params, 4) == HIDPP_LONG_LEN);
        memset(reply, 0, sizeof(reply));
        reply[0] = 0x11;
        reply[1] = rdev;
        reply[2] = ridx;
        reply[3] = buf[3]; /* same fn|swid */
        reply[4] = 0x7E;
        kind = hidpp_classify(reply, sizeof(reply), &devnum, &idx, &fnsw, params);
        CHECK(kind == HIDPP_REPLY_DATA);
        CHECK(devnum == rdev && idx == ridx && fnsw == buf[3]);
        CHECK(params[0] == 0x7E);
    }

    /* ---- error code names ---- */
    CHECK(strcmp(hidpp_strerror(1), "unknown") == 0);
    CHECK(strcmp(hidpp_strerror(2), "invalid argument") == 0);
    CHECK(strcmp(hidpp_strerror(5), "logitech internal") == 0);
    CHECK(strcmp(hidpp_strerror(6), "invalid feature index") == 0);
    CHECK(strcmp(hidpp_strerror(9), "unsupported") == 0);

    if (fails) {
        fprintf(stderr, "test_hidpp: %d check(s) failed\n", fails);
        return 1;
    }
    puts("test_hidpp: OK");
    return 0;
}
