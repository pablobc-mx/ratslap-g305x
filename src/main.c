/* stub — CLI implemented once the library modules are done. */
#include "util.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fputs("ratslap-g305x: not implemented yet\n", stderr);
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
