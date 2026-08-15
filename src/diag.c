#include "rin/diag.h"
#include <stdio.h>
#include <stdarg.h>

static int g_HadError = 0;

void
ReportError(int Line, const char *Format, ...)
{
    fprintf(stderr, "*** ERR: ");

    va_list Args;
    va_start(Args, Format);
    vfprintf(stderr, Format, Args);
    va_end(Args);

    fprintf(stderr, "\n     * at line: %d\n", Line);

    g_HadError = 1;
}

int
HadError(void)
{
    return g_HadError;
}