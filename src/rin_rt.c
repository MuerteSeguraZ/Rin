#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *
__rin_concat(char *a, char *b)
{
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    char *r = malloc(la + lb + 1);

    if (a)
        memcpy(r, a, la);
    if (b)
        memcpy(r + la, b, lb);
    r[la + lb] = '\0';
    return r;
}

char *
__rin_fmt_int(long long v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    return strdup(buf);
}

char *
__rin_fmt_char(int c)
{
    char buf[2];
    buf[0] = (char)c;
    buf[1] = '\0';
    return strdup(buf);
}

char *
__rin_fmt_float(double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v);
    return strdup(buf);
}

char *
__rin_fmt_str(char *s)
{
    return strdup(s ? s : "");
}

void
__rin_print(char *s)
{
    if (s)
        fputs(s, stdout);
}