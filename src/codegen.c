#include "rin/codegen.h"
#include "rin/diag.h"
#include <string.h>
#include <stdlib.h>

typedef struct VAR
{
    const char *Name;
    size_t Length;
    int SlotId;         /* the alloc'd temp holding the address */
    PRIN_TYPE Type;
    struct VAR *Next;
} VAR, *PVAR;

typedef struct
{
    FILE *Out;
    int TempCounter;
    int LabelCounter;
    int StrCounter;
    PVAR Vars;           /* flat list */
    RIN_MODULE *Module;
    int LastWasTerm;     /* true if the last emitted line was a block terminator */
} GEN, *PGEN;

static int
NewTemp(PGEN G)
{
    return G->TempCounter++;
}

static int
NewLabel(PGEN G)
{
    return G->LabelCounter++;
}

static char
BaseType(PRIN_TYPE T)
{
    switch (T->Kind)
    {
        case TY_LONG:
        case TY_POINTER:
            return 'l';
        case TY_FLOAT:
            return 's';
        case TY_VOID:
            return 0;
        default: /* int, bool, char */
            return 'w';
    }
}

static const char *
StoreOp(char Base)
{
    switch (Base)
    {
        case 'l': return "storel";
        case 's': return "stores";
        default:  return "storew";
    }
}

static const char *
LoadOp(char Base)
{
    switch (Base)
    {
        case 'l': return "loadl";
        case 's': return "loads";
        default:  return "loadw";
    }
}

static int
SlotSize(char Base)
{
    return Base == 'l' ? 8 : 4;
}

static int
NamesEqual(const char *A, size_t ALen, const char *B, size_t BLen)
{
    return ALen == BLen && strncmp(A, B, ALen) == 0;
}

static PVAR
FindVar(PGEN G, const char *Name, size_t Length)
{
    for (PVAR V = G->Vars; V; V = V->Next)
    {
        if (NamesEqual(V->Name, V->Length, Name, Length))
            return V;
    }
    return NULL;
}

static PVAR
DeclareVar(PGEN G, const char *Name, size_t Length, PRIN_TYPE Type)
{
    PVAR V = malloc(sizeof(VAR));
    V->Name = Name;
    V->Length = Length;
    V->Type = Type;
    V->SlotId = NewTemp(G);
    V->Next = G->Vars;
    G->Vars = V;

    char Base = BaseType(Type);
    fprintf(G->Out, "    %%t%d =l alloc%d %d\n", V->SlotId, SlotSize(Base), SlotSize(Base));
    return V;
}

static void
FreeVars(PGEN G)
{
    PVAR V = G->Vars;
    while (V)
    {
        PVAR Next = V->Next;
        free(V);
        V = Next;
    }
    G->Vars = NULL;
}

static RIN_FUNCTION *
FindFunc(PGEN G, const char *Name, size_t Length)
{
    for (size_t i = 0; i < G->Module->FunctionCount; i++)
    {
        RIN_FUNCTION *Fn = &G->Module->Functions[i];
        if (NamesEqual(Fn->Name, Fn->Length, Name, Length))
            return Fn;
    }
    return NULL;
}

static void CollectStringsExpr(PGEN G, PRIN_EXPR E);

static void
CollectStringsStmt(PGEN G, PRIN_STMT S)
{
    switch (S->Kind)
    {
        case STMT_LET:
        case STMT_MUT:
            if (S->As.VarDecl.Init)
                CollectStringsExpr(G, S->As.VarDecl.Init);
            break;
        case STMT_EXPR:
            CollectStringsExpr(G, S->As.ExprStmt.Expr);
            break;
        case STMT_RET:
            if (S->As.Ret.Value)
                CollectStringsExpr(G, S->As.Ret.Value);
            break;
        case STMT_IF:
            CollectStringsExpr(G, S->As.If.Cond);
            CollectStringsStmt(G, S->As.If.ThenBranch);
            if (S->As.If.ElseBranch)
                CollectStringsStmt(G, S->As.If.ElseBranch);
            break;
        case STMT_WHILE:
            CollectStringsExpr(G, S->As.While.Cond);
            CollectStringsStmt(G, S->As.While.Body);
            break;
        case STMT_FOR:
            if (S->As.For.Init)
                CollectStringsStmt(G, S->As.For.Init);
            if (S->As.For.Cond)
                CollectStringsExpr(G, S->As.For.Cond);
            if (S->As.For.Post)
                CollectStringsExpr(G, S->As.For.Post);
            CollectStringsStmt(G, S->As.For.Body);
            break;
        case STMT_BLOCK:
            for (size_t i = 0; i < S->As.Block.Count; i++)
                CollectStringsStmt(G, S->As.Block.Stmts[i]);
            break;
    }
}

static int
EmitStringData(PGEN G, const char *Chars, size_t Length)
{
    int Id = G->StrCounter++;
    fprintf(G->Out, "data $str.%d = { ", Id);

    int InString = 0; /* are we currently inside an open "..." segment */

    for (size_t i = 0; i < Length; i++)
    {
        unsigned char c = (unsigned char)Chars[i];

        if (c == '\\' && i + 1 < Length)
        {
            unsigned char Next = (unsigned char)Chars[i + 1];
            int ByteVal = -1;

            switch (Next)
            {
                case 'n': ByteVal = '\n'; break;
                case 't': ByteVal = '\t'; break;
                case 'r': ByteVal = '\r'; break;
                case '0': ByteVal = '\0'; break;
                case '\\': ByteVal = '\\'; break;
                case '"': ByteVal = '"'; break;
                case '\'': ByteVal = '\''; break;
                default: break; /* unrecognized escape: fall through, emit backslash literally below */
            }

            if (ByteVal >= 0)
            {
                if (InString)
                {
                    fprintf(G->Out, "\", ");
                    InString = 0;
                }
                fprintf(G->Out, "b %d, ", ByteVal);
                i++; /* consume the escape char too */
                continue;
            }
        }

        if (!InString)
        {
            fprintf(G->Out, "b \"");
            InString = 1;
        }

        if (c == '"' || c == '\\')
            fputc('\\', G->Out);
        fputc(c, G->Out);
    }

    if (InString)
        fprintf(G->Out, "\", ");

    fprintf(G->Out, "b 0 }\n");
    return Id;
}

static void
CollectStringsExpr(PGEN G, PRIN_EXPR E)
{
    switch (E->Kind)
    {
        case EXPR_STRING_LIT:
            E->As.StringLit.DataId = EmitStringData(G, E->As.StringLit.Chars, E->As.StringLit.Length);
            break;
        case EXPR_BINARY:
            CollectStringsExpr(G, E->As.Binary.Left);
            CollectStringsExpr(G, E->As.Binary.Right);
            break;
        case EXPR_UNARY:
            CollectStringsExpr(G, E->As.Unary.Operand);
            break;
        case EXPR_ASSIGN:
            CollectStringsExpr(G, E->As.Assign.Target);
            CollectStringsExpr(G, E->As.Assign.Value);
            break;
        case EXPR_CALL:
            if (E->As.Call.IsFmtBuiltin)
            {
                for (size_t s = 0; s < E->As.Call.FmtSegmentCount; s++)
                {
                    FMT_SEGMENT *Seg = &E->As.Call.FmtSegments[s];
                    if (Seg->IsLiteral)
                        Seg->DataId = EmitStringData(G, Seg->Text, Seg->Length);
                }
                for (size_t i = 1; i < E->As.Call.ArgCount; i++)
                    CollectStringsExpr(G, E->As.Call.Args[i]);
            }
            else
            {
                for (size_t i = 0; i < E->As.Call.ArgCount; i++)
                    CollectStringsExpr(G, E->As.Call.Args[i]);
            }
            break;
        default:
            break;
    }
}

static int
StmtAlwaysReturns(PRIN_STMT S)
{
    switch (S->Kind)
    {
        case STMT_RET:
            return 1;

        case STMT_BLOCK:
            return S->As.Block.Count > 0 &&
                   StmtAlwaysReturns(S->As.Block.Stmts[S->As.Block.Count - 1]);

        case STMT_IF:
            return S->As.If.ElseBranch != NULL &&
                   StmtAlwaysReturns(S->As.If.ThenBranch) &&
                   StmtAlwaysReturns(S->As.If.ElseBranch);

        default:
            return 0;
    }
}

static int GenExpr(PGEN G, PRIN_EXPR E);

static int
GenFmtSegmentValue(PGEN G, PRIN_EXPR CallExpr, FMT_SEGMENT *Seg)
{
    if (Seg->IsLiteral)
    {
        int T = NewTemp(G);
        fprintf(G->Out, "    %%t%d =l copy $str.%d\n", T, Seg->DataId);
        return T;
    }

    PRIN_EXPR Arg = CallExpr->As.Call.Args[Seg->ArgIndex];
    int ArgTemp = GenExpr(G, Arg);
    char ArgBase = BaseType(Arg->ResolvedType);
    int T = NewTemp(G);

    switch (Seg->Spec)
    {
        case 'd':
        {
            int LongTemp = ArgTemp;
            if (ArgBase == 'w')
            {
                LongTemp = NewTemp(G);
                fprintf(G->Out, "    %%t%d =l extsw %%t%d\n", LongTemp, ArgTemp);
            }
            fprintf(G->Out, "    %%t%d =l call $__rin_fmt_int(l %%t%d)\n", T, LongTemp);
            break;
        }
        case 's':
            fprintf(G->Out, "    %%t%d =l call $__rin_fmt_str(l %%t%d)\n", T, ArgTemp);
            break;
        case 'c':
            fprintf(G->Out, "    %%t%d =l call $__rin_fmt_char(w %%t%d)\n", T, ArgTemp);
            break;
        case 'f':
        {
            int DoubleTemp = NewTemp(G);
            fprintf(G->Out, "    %%t%d =d exts %%t%d\n", DoubleTemp, ArgTemp);
            fprintf(G->Out, "    %%t%d =l call $__rin_fmt_float(d %%t%d)\n", T, DoubleTemp);
            break;
        }
    }

    return T;
}

static int
GenFmtCall(PGEN G, PRIN_EXPR E)
{
    int Accum;

    if (E->As.Call.FmtSegmentCount == 0)
    {
        Accum = NewTemp(G);
        fprintf(G->Out, "    %%t%d =l copy $__rin_empty\n", Accum);
    }
    else
    {
        Accum = GenFmtSegmentValue(G, E, &E->As.Call.FmtSegments[0]);
        for (size_t s = 1; s < E->As.Call.FmtSegmentCount; s++)
        {
            int Next = GenFmtSegmentValue(G, E, &E->As.Call.FmtSegments[s]);
            int NewAccum = NewTemp(G);
            fprintf(G->Out, "    %%t%d =l call $__rin_concat(l %%t%d, l %%t%d)\n", NewAccum, Accum, Next);
            Accum = NewAccum;
        }
    }

    if (NamesEqual(E->As.Call.Name, E->As.Call.Length, "print", 5))
    {
        fprintf(G->Out, "    call $__rin_print(l %%t%d)\n", Accum);
    }

    return Accum;
}

static int GenExpr(PGEN G, PRIN_EXPR E);

static int
GenLValueAddr(PGEN G, PRIN_EXPR E)
{
    /* returns a temp holding the ADDRESS to store/load through */
    if (E->Kind == EXPR_IDENT)
    {
        PVAR V = FindVar(G, E->As.Ident.Name, E->As.Ident.Length);
        return V->SlotId; /* the alloc'd slot temp IS the address */
    }

    if (E->Kind == EXPR_UNARY && E->As.Unary.Op == UN_DEREF)
    {
        /* address is just the pointer's value */
        return GenExpr(G, E->As.Unary.Operand);
    }

    ReportError(E->Line, "internal codegen error: invalid lvalue");
    return NewTemp(G);
}

static const char *
BinOpName(BINARY_OP Op, char Base)
{
    switch (Op)
    {
        case BIN_ADD: return "add";
        case BIN_SUB: return "sub";
        case BIN_MUL: return "mul";
        case BIN_DIV: return Base == 's' ? "div" : "div";
        case BIN_MOD: return "rem";
        case BIN_BAND: return "and";
        case BIN_BOR: return "or";
        case BIN_BXOR: return "xor";
        case BIN_SHL: return "shl";
        case BIN_SHR: return "sar";
        default: return "add";
    }
}

static const char *
CmpOpName(BINARY_OP Op, char Base)
{
    if (Base == 's')
    {
        switch (Op)
        {
            case BIN_EQ: return "ceqs";
            case BIN_NEQ: return "cnes";
            case BIN_LT: return "clts";
            case BIN_GT: return "cgts";
            case BIN_LE: return "cles";
            case BIN_GE: return "cges";
            default: return "ceqs";
        }
    }

    const char *Suffix = Base == 'l' ? "l" : "w";
    static char Buf[16];
    const char *Base3;
    switch (Op)
    {
        case BIN_EQ: Base3 = "ceq"; break;
        case BIN_NEQ: Base3 = "cne"; break;
        case BIN_LT: Base3 = "cslt"; break;
        case BIN_GT: Base3 = "csgt"; break;
        case BIN_LE: Base3 = "csle"; break;
        case BIN_GE: Base3 = "csge"; break;
        default: Base3 = "ceq"; break;
    }
    snprintf(Buf, sizeof(Buf), "%s%s", Base3, Suffix);
    return Buf;
}

static int
GenExpr(PGEN G, PRIN_EXPR E)
{
    char Base = BaseType(E->ResolvedType);

    switch (E->Kind)
    {
        case EXPR_INT_LIT:
        {
            int T = NewTemp(G);
            fprintf(G->Out, "    %%t%d =%c copy %lld\n", T, Base, E->As.IntLit);
            return T;
        }

        case EXPR_FLOAT_LIT:
        {
            int T = NewTemp(G);
            fprintf(G->Out, "    %%t%d =s copy s_%.9g\n", T, E->As.FloatLit);
            return T;
        }

        case EXPR_BOOL_LIT:
        {
            int T = NewTemp(G);
            fprintf(G->Out, "    %%t%d =w copy %d\n", T, E->As.BoolLit ? 1 : 0);
            return T;
        }

        case EXPR_CHAR_LIT:
        {
            int T = NewTemp(G);
            fprintf(G->Out, "    %%t%d =w copy %d\n", T, (int)(unsigned char)E->As.CharLit);
            return T;
        }

        case EXPR_STRING_LIT:
        {
            int T = NewTemp(G);
            fprintf(G->Out, "    %%t%d =l copy $str.%d\n", T, E->As.StringLit.DataId);
            return T;
        }

        case EXPR_IDENT:
        {
            PVAR V = FindVar(G, E->As.Ident.Name, E->As.Ident.Length);
            int T = NewTemp(G);
            fprintf(G->Out, "    %%t%d =%c %s %%t%d\n", T, Base, LoadOp(Base), V->SlotId);
            return T;
        }

        case EXPR_BINARY:
        {
            char OperandBase = BaseType(E->As.Binary.Left->ResolvedType);
            int L = GenExpr(G, E->As.Binary.Left);
            int R = GenExpr(G, E->As.Binary.Right);
            int T = NewTemp(G);

            switch (E->As.Binary.Op)
            {
                case BIN_EQ: case BIN_NEQ: case BIN_LT: case BIN_GT: case BIN_LE: case BIN_GE:
                    fprintf(G->Out, "    %%t%d =w %s %%t%d, %%t%d\n", T, CmpOpName(E->As.Binary.Op, OperandBase), L, R);
                    break;

                case BIN_AND:
                case BIN_OR:
                    fprintf(G->Out, "    %%t%d =w %s %%t%d, %%t%d\n", T,
                            E->As.Binary.Op == BIN_AND ? "and" : "or", L, R);
                    break;

                default:
                    fprintf(G->Out, "    %%t%d =%c %s %%t%d, %%t%d\n", T, OperandBase,
                            BinOpName(E->As.Binary.Op, OperandBase), L, R);
                    break;
            }
            return T;
        }

        case EXPR_UNARY:
        {
            if (E->As.Unary.Op == UN_ADDR)
            {
                int Addr = GenLValueAddr(G, E->As.Unary.Operand);
                int T = NewTemp(G);
                fprintf(G->Out, "    %%t%d =l copy %%t%d\n", T, Addr);
                return T;
            }

            if (E->As.Unary.Op == UN_DEREF)
            {
                int Ptr = GenExpr(G, E->As.Unary.Operand);
                int T = NewTemp(G);
                fprintf(G->Out, "    %%t%d =%c %s %%t%d\n", T, Base, LoadOp(Base), Ptr);
                return T;
            }

            int Operand = GenExpr(G, E->As.Unary.Operand);
            int T = NewTemp(G);

            switch (E->As.Unary.Op)
            {
                case UN_NEG:
                    fprintf(G->Out, "    %%t%d =%c neg %%t%d\n", T, Base, Operand);
                    break;
                case UN_NOT:
                    fprintf(G->Out, "    %%t%d =w ceqw %%t%d, 0\n", T, Operand);
                    break;
                case UN_BNOT:
                    fprintf(G->Out, "    %%t%d =%c xor %%t%d, -1\n", T, Base, Operand);
                    break;
                default:
                    break;
            }
            return T;
        }

        case EXPR_ASSIGN:
        {
            int Value = GenExpr(G, E->As.Assign.Value);
            int Addr = GenLValueAddr(G, E->As.Assign.Target);
            char TargetBase = BaseType(E->As.Assign.Target->ResolvedType);
            fprintf(G->Out, "    %s %%t%d, %%t%d\n", StoreOp(TargetBase), Value, Addr);
            return Value;
        }

        case EXPR_CALL:
        {
            if (E->As.Call.IsFmtBuiltin)
                return GenFmtCall(G, E);

            int *ArgTemps = malloc(E->As.Call.ArgCount * sizeof(int));
            for (size_t i = 0; i < E->As.Call.ArgCount; i++)
                ArgTemps[i] = GenExpr(G, E->As.Call.Args[i]);

            RIN_FUNCTION *Callee = FindFunc(G, E->As.Call.Name, E->As.Call.Length);
            PRIN_TYPE RetType = Callee ? Callee->ReturnType : NULL;
            int T = -1;

            if (RetType && RetType->Kind != TY_VOID)
            {
                T = NewTemp(G);
                fprintf(G->Out, "    %%t%d =%c call $%.*s(", T, BaseType(RetType), (int)E->As.Call.Length, E->As.Call.Name);
            }
            else
            {
                fprintf(G->Out, "    call $%.*s(", (int)E->As.Call.Length, E->As.Call.Name);
            }

            size_t FixedCount = Callee ? Callee->ParamCount : E->As.Call.ArgCount;
            for (size_t i = 0; i < E->As.Call.ArgCount; i++)
            {
                if (Callee && Callee->IsVariadic && i == FixedCount)
                    fprintf(G->Out, "%s...", i == 0 ? "" : ", ");

                char ArgBase = BaseType(E->As.Call.Args[i]->ResolvedType);
                fprintf(G->Out, "%s%c %%t%d", i == 0 ? "" : ", ", ArgBase, ArgTemps[i]);
            }
            /* if the call passed no variadic args at all, the '...' marker still needs to be there */
            if (Callee && Callee->IsVariadic && E->As.Call.ArgCount == FixedCount)
                fprintf(G->Out, "%s...", FixedCount == 0 ? "" : ", ");
            fprintf(G->Out, ")\n");

            free(ArgTemps);
            return T >= 0 ? T : NewTemp(G); /* dummy temp for void calls used in expr position (shouldn't happen post-typecheck) */
        }
    }

    return NewTemp(G);
}

static void GenStmt(PGEN G, PRIN_STMT S);

static void
GenBlock(PGEN G, PRIN_STMT Block)
{
    for (size_t i = 0; i < Block->As.Block.Count; i++)
        GenStmt(G, Block->As.Block.Stmts[i]);
}

static void
GenStmt(PGEN G, PRIN_STMT S)
{
    switch (S->Kind)
    {
        case STMT_LET:
        case STMT_MUT:
        {
            PVAR V = DeclareVar(G, S->As.VarDecl.Name, S->As.VarDecl.Length, S->As.VarDecl.Type);
            if (S->As.VarDecl.Init)
            {
                int Val = GenExpr(G, S->As.VarDecl.Init);
                char Base = BaseType(S->As.VarDecl.Type);
                fprintf(G->Out, "    %s %%t%d, %%t%d\n", StoreOp(Base), Val, V->SlotId);
            }
            break;
        }

        case STMT_EXPR:
            GenExpr(G, S->As.ExprStmt.Expr);
            break;

        case STMT_RET:
            if (S->As.Ret.Value)
            {
                int Val = GenExpr(G, S->As.Ret.Value);
                fprintf(G->Out, "    ret %%t%d\n", Val);
            }
            else
            {
                fprintf(G->Out, "    ret\n");
            }
            break;

        case STMT_IF:
        {
            int Cond = GenExpr(G, S->As.If.Cond);
            int LThen = NewLabel(G);
            int LElse = NewLabel(G);
            int LEnd = NewLabel(G);

            fprintf(G->Out, "    jnz %%t%d, @L%d, @L%d\n", Cond, LThen, S->As.If.ElseBranch ? LElse : LEnd);

            fprintf(G->Out, "@L%d\n", LThen);
            GenBlock(G, S->As.If.ThenBranch);
            if (!StmtAlwaysReturns(S->As.If.ThenBranch))
                fprintf(G->Out, "    jmp @L%d\n", LEnd);

            if (S->As.If.ElseBranch)
            {
                fprintf(G->Out, "@L%d\n", LElse);
                if (S->As.If.ElseBranch->Kind == STMT_BLOCK)
                    GenBlock(G, S->As.If.ElseBranch);
                else
                    GenStmt(G, S->As.If.ElseBranch);
                if (!StmtAlwaysReturns(S->As.If.ElseBranch))
                    fprintf(G->Out, "    jmp @L%d\n", LEnd);
            }

            fprintf(G->Out, "@L%d\n", LEnd);
            break;
        }

        case STMT_WHILE:
        {
            int LCond = NewLabel(G);
            int LBody = NewLabel(G);
            int LEnd = NewLabel(G);

            fprintf(G->Out, "    jmp @L%d\n", LCond);
            fprintf(G->Out, "@L%d\n", LCond);
            int Cond = GenExpr(G, S->As.While.Cond);
            fprintf(G->Out, "    jnz %%t%d, @L%d, @L%d\n", Cond, LBody, LEnd);

            fprintf(G->Out, "@L%d\n", LBody);
            GenBlock(G, S->As.While.Body);
            if (!StmtAlwaysReturns(S->As.While.Body))
                fprintf(G->Out, "    jmp @L%d\n", LCond);

            fprintf(G->Out, "@L%d\n", LEnd);
            break;
        }

        case STMT_FOR:
        {
            if (S->As.For.Init)
                GenStmt(G, S->As.For.Init);

            int LCond = NewLabel(G);
            int LBody = NewLabel(G);
            int LEnd = NewLabel(G);

            fprintf(G->Out, "    jmp @L%d\n", LCond);
            fprintf(G->Out, "@L%d\n", LCond);

            if (S->As.For.Cond)
            {
                int Cond = GenExpr(G, S->As.For.Cond);
                fprintf(G->Out, "    jnz %%t%d, @L%d, @L%d\n", Cond, LBody, LEnd);
            }
            else
            {
                fprintf(G->Out, "    jmp @L%d\n", LBody);
            }

            fprintf(G->Out, "@L%d\n", LBody);
            GenBlock(G, S->As.For.Body);
            if (S->As.For.Post)
                GenExpr(G, S->As.For.Post);
            if (!StmtAlwaysReturns(S->As.For.Body))
                fprintf(G->Out, "    jmp @L%d\n", LCond);

            fprintf(G->Out, "@L%d\n", LEnd);
            break;
        }

        case STMT_BLOCK:
            GenBlock(G, S);
            break;
    }
}

static void
GenFunction(PGEN G, RIN_FUNCTION *Fn)
{
    FreeVars(G);
    G->TempCounter = 0;
    G->LabelCounter = 0;

    int IsMain = NamesEqual(Fn->Name, Fn->Length, "main", 4);
    (void)IsMain;
    char RetBase = BaseType(Fn->ReturnType);

    fprintf(G->Out, "export function ");
    if (RetBase)
        fprintf(G->Out, "%c ", RetBase);
    fprintf(G->Out, "$%.*s(", (int)Fn->Length, Fn->Name);

    int *ParamTemps = malloc(Fn->ParamCount * sizeof(int));
    for (size_t i = 0; i < Fn->ParamCount; i++)
    {
        ParamTemps[i] = NewTemp(G);
        char PBase = BaseType(Fn->Params[i].Type);
        fprintf(G->Out, "%s%c %%t%d", i == 0 ? "" : ", ", PBase, ParamTemps[i]);
    }
    fprintf(G->Out, ") {\n@start\n");

    for (size_t i = 0; i < Fn->ParamCount; i++)
    {
        PVAR V = DeclareVar(G, Fn->Params[i].Name, Fn->Params[i].Length, Fn->Params[i].Type);
        char PBase = BaseType(Fn->Params[i].Type);
        fprintf(G->Out, "    %s %%t%d, %%t%d\n", StoreOp(PBase), ParamTemps[i], V->SlotId);
    }
    free(ParamTemps);

    GenBlock(G, Fn->Body);

    if (!StmtAlwaysReturns(Fn->Body))
    {
        if (Fn->ReturnType->Kind == TY_VOID)
            fprintf(G->Out, "    ret\n");
        else
            fprintf(G->Out, "    ret 0\n");
    }

    fprintf(G->Out, "}\n\n");
}

int
CodegenModule(PRIN_MODULE Module, FILE *Out)
{
    GEN G;
    G.Out = Out;
    G.TempCounter = 0;
    G.LabelCounter = 0;
    G.StrCounter = 0;
    G.Vars = NULL;
    G.Module = Module;

    fprintf(G.Out, "data $__rin_empty = { b 0 }\n");

    for (size_t i = 0; i < Module->FunctionCount; i++)
    {
        if (!Module->Functions[i].IsExtern)
            CollectStringsStmt(&G, Module->Functions[i].Body);
    }

    for (size_t i = 0; i < Module->FunctionCount; i++)
    {
        if (!Module->Functions[i].IsExtern)
            GenFunction(&G, &Module->Functions[i]);
    }

    FreeVars(&G);
    return !HadError();
}