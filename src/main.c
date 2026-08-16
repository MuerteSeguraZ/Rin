#include "rin/parser.h"
#include "rin/typecheck.h"
#include "rin/codegen.h"
#include "rin/diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_RuntimeSrc =
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <string.h>\n"
"\n"
"char *__rin_concat(char *a, char *b)\n"
"{\n"
"    size_t la = a ? strlen(a) : 0;\n"
"    size_t lb = b ? strlen(b) : 0;\n"
"    char *r = malloc(la + lb + 1);\n"
"    if (a) memcpy(r, a, la);\n"
"    if (b) memcpy(r + la, b, lb);\n"
"    r[la + lb] = '\\0';\n"
"    return r;\n"
"}\n"
"\n"
"char *__rin_fmt_int(long long v)\n"
"{\n"
"    char buf[32];\n"
"    snprintf(buf, sizeof(buf), \"%lld\", v);\n"
"    return strdup(buf);\n"
"}\n"
"\n"
"char *__rin_fmt_uint(unsigned long long v)\n"
"{\n"
"    char buf[32];\n"
"    snprintf(buf, sizeof(buf), \"%llu\", v);\n"
"    return strdup(buf);\n"
"}\n"
"\n"
"char *__rin_fmt_char(int c)\n"
"{\n"
"    char buf[2];\n"
"    buf[0] = (char)c;\n"
"    buf[1] = '\\0';\n"
"    return strdup(buf);\n"
"}\n"
"\n"
"char *__rin_fmt_float(double v)\n"
"{\n"
"    char buf[64];\n"
"    snprintf(buf, sizeof(buf), \"%g\", v);\n"
"    return strdup(buf);\n"
"}\n"
"\n"
"char *__rin_fmt_str(char *s)\n"
"{\n"
"    return strdup(s ? s : \"\");\n"
"}\n"
"\n"
"void __rin_print(char *s)\n"
"{\n"
"    if (s) fputs(s, stdout);\n"
"}\n";

static char *
ReadFile(const char *Path, size_t *OutLength)
{
    FILE *F = fopen(Path, "rb");
    if (!F)
    {
        fprintf(stderr, "*** ERR: could not open '%s'\n", Path);
        return NULL;
    }

    fseek(F, 0, SEEK_END);
    long Size = ftell(F);
    fseek(F, 0, SEEK_SET);

    char *Buf = malloc((size_t)Size + 1);
    fread(Buf, 1, (size_t)Size, F);
    Buf[Size] = '\0';
    fclose(F);

    *OutLength = (size_t)Size;
    return Buf;
}

static void
PrintUsage(const char *Prog)
{
    fprintf(stderr, "usage: %s <input.rn> -conv <output>\n", Prog);
    fprintf(stderr, "       %s <input.rn> -S              (emit QBE IR only)\n", Prog);
}

int
main(int argc, char **argv)
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    const char *InputPath = argv[1];
    const char *OutputPath = NULL;
    int EmitIrOnly = 0;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-conv") == 0 && i + 1 < argc)
        {
            OutputPath = argv[++i];
        }
        else if (strcmp(argv[i], "-S") == 0)
        {
            EmitIrOnly = 1;
        }
        else
        {
            fprintf(stderr, "*** ERR: unrecognized argument '%s'\n", argv[i]);
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (!EmitIrOnly && !OutputPath)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    size_t SrcLen;
    char *Src = ReadFile(InputPath, &SrcLen);
    if (!Src)
        return 1;

    ARENA Arena;
    ArenaInit(&Arena);

    PRIN_MODULE Mod = ParseModule(Src, SrcLen, &Arena);
    if (!Mod)
    {
        ArenaFree(&Arena);
        free(Src);
        return 1;
    }

    if (!TypecheckModule(Mod, &Arena))
    {
        ArenaFree(&Arena);
        free(Src);
        return 1;
    }

    /* always write IR to a temp .qbe file first, whether we stop there or go further */
    char IrPath[512];
    snprintf(IrPath, sizeof(IrPath), "%s.qbe", EmitIrOnly ? OutputPath ? OutputPath : "a" : OutputPath);

    FILE *IrFile = fopen(IrPath, "w");
    if (!IrFile)
    {
        fprintf(stderr, "*** ERR: could not write to '%s'\n", IrPath);
        ArenaFree(&Arena);
        free(Src);
        return 1;
    }

    int CodegenOk = CodegenModule(Mod, IrFile);
    fclose(IrFile);

    ArenaFree(&Arena);
    free(Src);

    if (!CodegenOk)
        return 1;

    if (EmitIrOnly)
    {
        printf("wrote %s\n", IrPath);
        return 0;
    }

    /* shell out to qbe to produce assembly, then cc to assemble+link */
    char AsmPath[512];
    snprintf(AsmPath, sizeof(AsmPath), "%s.s", OutputPath);

    char RtSrcPath[512];
    snprintf(RtSrcPath, sizeof(RtSrcPath), "%s.rt.c", OutputPath);

    FILE *RtFile = fopen(RtSrcPath, "w");
    if (!RtFile)
    {
        fprintf(stderr, "*** ERR: could not write to '%s'\n", RtSrcPath);
        return 1;
    }
    fputs(g_RuntimeSrc, RtFile);
    fclose(RtFile);

    char Cmd[2048];
    snprintf(Cmd, sizeof(Cmd), "qbe -o \"%s\" \"%s\"", AsmPath, IrPath);
    if (system(Cmd) != 0)
    {
        fprintf(stderr, "*** ERR: qbe failed to compile IR\n");
        remove(RtSrcPath);
        return 1;
    }

    snprintf(Cmd, sizeof(Cmd), "cc \"%s\" \"%s\" -o \"%s\"", AsmPath, RtSrcPath, OutputPath);
    if (system(Cmd) != 0)
    {
        fprintf(stderr, "*** ERR: cc failed to assemble/link\n");
        remove(RtSrcPath);
        return 1;
    }

    /* clean up intermediate files */
    remove(IrPath);
    remove(AsmPath);
    remove(RtSrcPath);

    printf("wrote %s\n", OutputPath);
    return 0;
}