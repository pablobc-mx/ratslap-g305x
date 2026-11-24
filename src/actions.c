/* stub — binding catalog implemented after the tests exist. */
#include "actions.h"

int rslap_parse_binding(const char *token, uint8_t out[4])
{
    (void)token; (void)out;
    return RSLAP_RC_MALFORMED;
}

void rslap_describe_binding(const uint8_t raw[4], char *out, size_t outlen)
{
    (void)raw;
    if (outlen > 0)
        out[0] = '\0';
}

void rslap_print_keylist(FILE *f)
{
    (void)f;
}
