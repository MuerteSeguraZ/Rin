#include "rin/typecheck.h"
#include "rin/diag.h"
#include <string.h>
#include <stdio.h>

typedef struct SYMBOL
{
    const char *Name;
    size_t Length;
    PRIN_TYPE Type;
    int IsMutable;
    struct SYMBOL *Next;
} SYMBOL, *PSYMBOL;

typedef struct SCOPE
{
    PSYMBOL Symbols;
    struct SCOPE *Parent;
} SCOPE, *PSCOPE;

typedef struct
{
    const char *Name;
    size_t Length;
    RIN_PARAM *Params;
    size_t ParamCount;
    PRIN_TYPE ReturnType;
    int IsVariadic;
} FUNC_SIG;

typedef struct
{
    PARENA Arena;
    PSCOPE CurrentScope;
    FUNC_SIG *Funcs;
    size_t FuncCount;
    PRIN_TYPE CurrentReturnType; /* return type of the function currently being checked */
} CHECKER, *PCHECKER;

static PSCOPE
PushScope(PCHECKER C)
{
    PSCOPE S = ArenaAlloc(C->Arena, sizeof(SCOPE));
    S->Symbols = NULL;
    S->Parent = C->CurrentScope;
    C->CurrentScope = S;
    return S;
}

static void
PopScope(PCHECKER C)
{
    C->CurrentScope = C->CurrentScope->Parent;
}

static int
NamesEqual(const char *A, size_t ALen, const char *B, size_t BLen)
{
    return ALen == BLen && strncmp(A, B, ALen) == 0;
}

static PSYMBOL
LookupSymbol(PCHECKER C, const char *Name, size_t Length)
{
    for (PSCOPE Scope = C->CurrentScope; Scope; Scope = Scope->Parent)
    {
        for (PSYMBOL Sym = Scope->Symbols; Sym; Sym = Sym->Next)
        {
            if (NamesEqual(Sym->Name, Sym->Length, Name, Length))
                return Sym;
        }
    }
    return NULL;
}

static void
DeclareSymbol(PCHECKER C, const char *Name, size_t Length, PRIN_TYPE Type, int IsMutable, int Line)
{
    for (PSYMBOL Sym = C->CurrentScope->Symbols; Sym; Sym = Sym->Next)
    {
        if (NamesEqual(Sym->Name, Sym->Length, Name, Length))
        {
            ReportError(Line, "redeclaration of '%.*s'", (int)Length, Name);
            return;
        }
    }

    PSYMBOL Sym = ArenaAlloc(C->Arena, sizeof(SYMBOL));
    Sym->Name = Name;
    Sym->Length = Length;
    Sym->Type = Type;
    Sym->IsMutable = IsMutable;
    Sym->Next = C->CurrentScope->Symbols;
    C->CurrentScope->Symbols = Sym;
}

static FUNC_SIG *
LookupFunc(PCHECKER C, const char *Name, size_t Length)
{
    for (size_t i = 0; i < C->FuncCount; i++)
    {
        if (NamesEqual(C->Funcs[i].Name, C->Funcs[i].Length, Name, Length))
            return &C->Funcs[i];
    }
    return NULL;
}

static int
TypesEqual(PRIN_TYPE A, PRIN_TYPE B)
{
    if (A->Kind != B->Kind)
        return 0;
    if (A->Kind == TY_POINTER)
        return TypesEqual(A->PointeeType, B->PointeeType);
    return 1;
}

static int
IsIntegerKind(TYPE_KIND K)
{
    switch (K)
    {
        case TY_INT: case TY_LONG: case TY_CHAR:
        case TY_I8: case TY_I16:
        case TY_U8: case TY_U16: case TY_U32: case TY_U64:
            return 1;
        default:
            return 0;
    }
}

static int
IntWidth(TYPE_KIND K)
{
    switch (K)
    {
        case TY_I8: case TY_U8: case TY_CHAR: case TY_BOOL: return 1;
        case TY_I16: case TY_U16: return 2;
        case TY_INT: case TY_U32: return 4;
        case TY_LONG: case TY_U64: return 8;
        default: return 0;
    }
}

static int
IsNumeric(PRIN_TYPE T)
{
    return IsIntegerKind(T->Kind) || T->Kind == TY_FLOAT;
}

static const char *
TypeName(PRIN_TYPE T)
{
    switch (T->Kind)
    {
        case TY_VOID: return "void";
        case TY_INT: return "int";
        case TY_LONG: return "long";
        case TY_CHAR: return "char";
        case TY_BOOL: return "bool";
        case TY_FLOAT: return "float";
        case TY_POINTER: return "pointer";
        case TY_I8: return "i8";
        case TY_I16: return "i16";
        case TY_U8: return "u8";
        case TY_U16: return "u16";
        case TY_U32: return "u32";
        case TY_U64: return "u64";
    }
    return "?";
}

static PRIN_TYPE
MakeType(PCHECKER C, TYPE_KIND Kind)
{
    PRIN_TYPE T = ArenaAlloc(C->Arena, sizeof(RIN_TYPE));
    T->Kind = Kind;
    T->PointeeType = NULL;
    return T;
}

static PRIN_TYPE
MakePointerToChar(PCHECKER C)
{
    PRIN_TYPE Ptr = MakeType(C, TY_POINTER);
    Ptr->PointeeType = MakeType(C, TY_CHAR);
    return Ptr;
}

static PRIN_TYPE
PromoteIntTypes(PCHECKER C, PRIN_TYPE L, PRIN_TYPE R)
{
    if (L->Kind == R->Kind)
        return L;

    int WidthL = IntWidth(L->Kind);
    int WidthR = IntWidth(R->Kind);

    if (WidthL == WidthR)
        return L; /* e.g. int vs u32 collision, keep left operand's type */

    return WidthL > WidthR ? L : R;
}

static int
CoerceLiteralTo(PRIN_EXPR E, PRIN_TYPE Target)
{
    if (!IsIntegerKind(Target->Kind))
        return 0;

    if (E->Kind == EXPR_INT_LIT)
    {
        E->ResolvedType = Target;
        return 1;
    }

    if (E->Kind == EXPR_UNARY && E->As.Unary.Op == UN_NEG &&
        E->As.Unary.Operand->Kind == EXPR_INT_LIT)
    {
        E->ResolvedType = Target;
        E->As.Unary.Operand->ResolvedType = Target;
        return 1;
    }

    return 0;
}

static PRIN_TYPE CheckExpr(PCHECKER C, PRIN_EXPR E); /* forward decl, needed by CheckFmtCall below */

static int
IsFmtOrPrintName(const char *Name, size_t Length)
{
    return NamesEqual(Name, Length, "fmt", 3) || NamesEqual(Name, Length, "print", 5);
}

static const char *
SpecTypeName(char Spec)
{
    switch (Spec)
    {
        case 'd': return "an integer type";
        case 's': return "char*";
        case 'c': return "char";
        case 'f': return "float";
        default: return "?";
    }
}

static int
SpecMatchesType(char Spec, PRIN_TYPE T)
{
    switch (Spec)
    {
        case 'd':
            return IsIntegerKind(T->Kind);
        case 's':
            return T->Kind == TY_POINTER && T->PointeeType->Kind == TY_CHAR;
        case 'c':
            return T->Kind == TY_CHAR;
        case 'f':
            return T->Kind == TY_FLOAT;
        default:
            return 0;
    }
}

static PRIN_TYPE
CheckFmtCall(PCHECKER C, PRIN_EXPR E)
{
    int IsPrint = NamesEqual(E->As.Call.Name, E->As.Call.Length, "print", 5);

    if (E->As.Call.ArgCount == 0 || E->As.Call.Args[0]->Kind != EXPR_STRING_LIT)
    {
        ReportError(E->Line, "'%.*s' requires a literal string as its first argument",
                    (int)E->As.Call.Length, E->As.Call.Name);
        for (size_t i = 0; i < E->As.Call.ArgCount; i++)
            CheckExpr(C, E->As.Call.Args[i]);
        return IsPrint ? MakeType(C, TY_VOID) : MakePointerToChar(C);
    }

    for (size_t i = 1; i < E->As.Call.ArgCount; i++)
        CheckExpr(C, E->As.Call.Args[i]);

    const char *Fmt = E->As.Call.Args[0]->As.StringLit.Chars;
    size_t FmtLen = E->As.Call.Args[0]->As.StringLit.Length;

    FMT_SEGMENT *Segments = ArenaAlloc(C->Arena, (FmtLen + 1) * sizeof(FMT_SEGMENT));
    size_t SegCount = 0;

    size_t LitStart = 0;
    size_t NextArg = 1;
    int Ok = 1;

    for (size_t i = 0; i < FmtLen; i++)
    {
        if (Fmt[i] != '%')
            continue;

        if (i + 1 >= FmtLen)
        {
            ReportError(E->Line, "dangling '%%' at end of format string");
            Ok = 0;
            break;
        }

        char Spec = Fmt[i + 1];

        if (Spec == '%')
        {
            if (i > LitStart)
            {
                Segments[SegCount].IsLiteral = 1;
                Segments[SegCount].Text = Fmt + LitStart;
                Segments[SegCount].Length = i - LitStart;
                SegCount++;
            }
            Segments[SegCount].IsLiteral = 1;
            Segments[SegCount].Text = "%";
            Segments[SegCount].Length = 1;
            SegCount++;
            i++; /* skip the second '%' */
            LitStart = i + 1;
            continue;
        }

        if (Spec != 'd' && Spec != 's' && Spec != 'c' && Spec != 'f')
        {
            ReportError(E->Line, "unknown format specifier '%%%c'", Spec);
            Ok = 0;
            i++;
            continue;
        }

        /* flush preceding literal text */
        if (i > LitStart)
        {
            Segments[SegCount].IsLiteral = 1;
            Segments[SegCount].Text = Fmt + LitStart;
            Segments[SegCount].Length = i - LitStart;
            SegCount++;
        }

        if (NextArg >= E->As.Call.ArgCount)
        {
            ReportError(E->Line, "'%%%c' has no matching argument", Spec);
            Ok = 0;
        }
        else
        {
            PRIN_TYPE ArgType = E->As.Call.Args[NextArg]->ResolvedType;
            if (!SpecMatchesType(Spec, ArgType))
            {
                ReportError(E->Line, "'%%%c' expects '%s'", Spec, SpecTypeName(Spec));
                Ok = 0;
            }
        }

        Segments[SegCount].IsLiteral = 0;
        Segments[SegCount].ArgIndex = (int)NextArg;
        Segments[SegCount].Spec = Spec;
        SegCount++;

        NextArg++;
        i++; /* skip spec char */
        LitStart = i + 1;
    }

    if (LitStart < FmtLen)
    {
        Segments[SegCount].IsLiteral = 1;
        Segments[SegCount].Text = Fmt + LitStart;
        Segments[SegCount].Length = FmtLen - LitStart;
        SegCount++;
    }

    if (NextArg != E->As.Call.ArgCount)
    {
        ReportError(E->Line, "'%.*s' has %zu extra argument(s) beyond its format placeholders",
                    (int)E->As.Call.Length, E->As.Call.Name, E->As.Call.ArgCount - NextArg);
        Ok = 0;
    }

    (void)Ok;

    E->As.Call.IsFmtBuiltin = 1;
    E->As.Call.FmtSegments = Segments;
    E->As.Call.FmtSegmentCount = SegCount;

    return IsPrint ? MakeType(C, TY_VOID) : MakePointerToChar(C);
}

static PRIN_TYPE CheckExpr(PCHECKER C, PRIN_EXPR E);

static PRIN_TYPE
CheckBinary(PCHECKER C, PRIN_EXPR E)
{
    PRIN_TYPE L = CheckExpr(C, E->As.Binary.Left);
    PRIN_TYPE R = CheckExpr(C, E->As.Binary.Right);
    BINARY_OP Op = E->As.Binary.Op;

    if (IsIntegerKind(L->Kind) && CoerceLiteralTo(E->As.Binary.Right, L))
        R = L;
    else if (IsIntegerKind(R->Kind) && CoerceLiteralTo(E->As.Binary.Left, R))
        L = R;

    E->As.Binary.OperandType = NULL;

    switch (Op)
    {
        case BIN_ADD: case BIN_SUB: case BIN_MUL: case BIN_DIV: case BIN_MOD:
        {
            if (!IsNumeric(L) || !IsNumeric(R))
            {
                ReportError(E->Line, "arithmetic requires numeric operands, got '%s' and '%s'", TypeName(L), TypeName(R));
                return MakeType(C, TY_INT);
            }
            PRIN_TYPE Result = (L->Kind == TY_FLOAT || R->Kind == TY_FLOAT)
                ? MakeType(C, TY_FLOAT)
                : PromoteIntTypes(C, L, R);
            E->As.Binary.OperandType = Result;
            return Result;
        }

        case BIN_EQ: case BIN_NEQ:
        {
            if (IsNumeric(L) && IsNumeric(R))
                E->As.Binary.OperandType = (L->Kind == TY_FLOAT || R->Kind == TY_FLOAT)
                    ? MakeType(C, TY_FLOAT) : PromoteIntTypes(C, L, R);
            else if (!TypesEqual(L, R))
                ReportError(E->Line, "cannot compare '%s' with '%s'", TypeName(L), TypeName(R));
            else
                E->As.Binary.OperandType = L;
            return MakeType(C, TY_BOOL);
        }

        case BIN_LT: case BIN_GT: case BIN_LE: case BIN_GE:
        {
            if (!IsNumeric(L) || !IsNumeric(R))
            {
                ReportError(E->Line, "comparison requires numeric operands, got '%s' and '%s'", TypeName(L), TypeName(R));
                E->As.Binary.OperandType = MakeType(C, TY_INT);
            }
            else
            {
                E->As.Binary.OperandType = (L->Kind == TY_FLOAT || R->Kind == TY_FLOAT)
                    ? MakeType(C, TY_FLOAT) : PromoteIntTypes(C, L, R);
            }
            return MakeType(C, TY_BOOL);
        }

        case BIN_AND: case BIN_OR:
            if (L->Kind != TY_BOOL || R->Kind != TY_BOOL)
                ReportError(E->Line, "logical operator requires bool operands, got '%s' and '%s'", TypeName(L), TypeName(R));
            return MakeType(C, TY_BOOL);

        case BIN_BAND: case BIN_BOR: case BIN_BXOR: case BIN_SHL: case BIN_SHR:
        {
            if (!IsIntegerKind(L->Kind) || !IsIntegerKind(R->Kind))
            {
                ReportError(E->Line, "bitwise operator requires integer operands, got '%s' and '%s'", TypeName(L), TypeName(R));
                E->As.Binary.OperandType = MakeType(C, TY_INT);
                return E->As.Binary.OperandType;
            }
            PRIN_TYPE Result = PromoteIntTypes(C, L, R);
            E->As.Binary.OperandType = Result;
            return Result;
        }
    }

    return MakeType(C, TY_INT);
}

static PRIN_TYPE
CheckUnary(PCHECKER C, PRIN_EXPR E)
{
    PRIN_TYPE Operand = CheckExpr(C, E->As.Unary.Operand);

    switch (E->As.Unary.Op)
    {
        case UN_NEG:
            if (!IsNumeric(Operand))
                ReportError(E->Line, "unary '-' requires a numeric operand, got '%s'", TypeName(Operand));
            return Operand;

        case UN_NOT:
            if (Operand->Kind != TY_BOOL)
                ReportError(E->Line, "'!' requires a bool operand, got '%s'", TypeName(Operand));
            return MakeType(C, TY_BOOL);

        case UN_BNOT:
            if (!IsNumeric(Operand) || Operand->Kind == TY_FLOAT)
                ReportError(E->Line, "'~' requires an integer operand, got '%s'", TypeName(Operand));
            return Operand;

        case UN_ADDR:
        {
            PRIN_TYPE Ptr = MakeType(C, TY_POINTER);
            Ptr->PointeeType = Operand;
            return Ptr;
        }

        case UN_DEREF:
            if (Operand->Kind != TY_POINTER)
            {
                ReportError(E->Line, "cannot dereference non-pointer type '%s'", TypeName(Operand));
                return MakeType(C, TY_INT);
            }
            return Operand->PointeeType;
    }

    return MakeType(C, TY_INT);
}

static PRIN_TYPE
CheckExpr(PCHECKER C, PRIN_EXPR E)
{
    PRIN_TYPE Result;

    switch (E->Kind)
    {
        case EXPR_INT_LIT:
            Result = MakeType(C, TY_INT);
            break;

        case EXPR_FLOAT_LIT:
            Result = MakeType(C, TY_FLOAT);
            break;

        case EXPR_STRING_LIT:
        {
            PRIN_TYPE Ptr = MakeType(C, TY_POINTER);
            Ptr->PointeeType = MakeType(C, TY_CHAR);
            Result = Ptr;
            break;
        }

        case EXPR_CHAR_LIT:
            Result = MakeType(C, TY_CHAR);
            break;

        case EXPR_BOOL_LIT:
            Result = MakeType(C, TY_BOOL);
            break;

        case EXPR_IDENT:
        {
            PSYMBOL Sym = LookupSymbol(C, E->As.Ident.Name, E->As.Ident.Length);
            if (!Sym)
            {
                ReportError(E->Line, "undeclared identifier '%.*s'", (int)E->As.Ident.Length, E->As.Ident.Name);
                Result = MakeType(C, TY_INT);
            }
            else
            {
                Result = Sym->Type;
            }
            break;
        }

        case EXPR_BINARY:
            Result = CheckBinary(C, E);
            break;

        case EXPR_UNARY:
            Result = CheckUnary(C, E);
            break;

        case EXPR_ASSIGN:
        {
            PRIN_TYPE ValueType = CheckExpr(C, E->As.Assign.Value);

            if (E->As.Assign.Target->Kind == EXPR_IDENT)
            {
                PSYMBOL Sym = LookupSymbol(C, E->As.Assign.Target->As.Ident.Name, E->As.Assign.Target->As.Ident.Length);
                if (!Sym)
                {
                    ReportError(E->Line, "undeclared identifier '%.*s'",
                                (int)E->As.Assign.Target->As.Ident.Length, E->As.Assign.Target->As.Ident.Name);
                    Result = ValueType;
                    break;
                }
                if (!Sym->IsMutable)
                {
                    ReportError(E->Line, "cannot assign to immutable '%.*s'", (int)Sym->Length, Sym->Name);
                }
                if (!TypesEqual(Sym->Type, ValueType) && !CoerceLiteralTo(E->As.Assign.Value, Sym->Type))
                {
                    ReportError(E->Line, "cannot assign '%s' to '%s'", TypeName(ValueType), TypeName(Sym->Type));
                }
                E->As.Assign.Target->ResolvedType = Sym->Type;
                Result = Sym->Type;
            }
            else
            {
                /* deref assignment target */
                PRIN_TYPE TargetType = CheckExpr(C, E->As.Assign.Target);
                if (!TypesEqual(TargetType, ValueType) && !CoerceLiteralTo(E->As.Assign.Value, TargetType))
                    ReportError(E->Line, "cannot assign '%s' to '%s'", TypeName(ValueType), TypeName(TargetType));
                Result = TargetType;
            }
            break;
        }

        case EXPR_CALL:
        {
            if (IsFmtOrPrintName(E->As.Call.Name, E->As.Call.Length))
            {
                Result = CheckFmtCall(C, E);
                break;
            }

            FUNC_SIG *Sig = LookupFunc(C, E->As.Call.Name, E->As.Call.Length);
            if (!Sig)
            {
                ReportError(E->Line, "call to undeclared function '%.*s'", (int)E->As.Call.Length, E->As.Call.Name);
                for (size_t i = 0; i < E->As.Call.ArgCount; i++)
                    CheckExpr(C, E->As.Call.Args[i]);
                Result = MakeType(C, TY_INT);
                break;
            }

            if (E->As.Call.ArgCount != Sig->ParamCount)
            {
                if (!(Sig->IsVariadic && E->As.Call.ArgCount >= Sig->ParamCount))
                {
                    ReportError(E->Line, "'%.*s' expects %s%zu argument(s), got %zu",
                                (int)E->As.Call.Length, E->As.Call.Name,
                                Sig->IsVariadic ? "at least " : "",
                                Sig->ParamCount, E->As.Call.ArgCount);
                }
            }

            size_t CheckCount = E->As.Call.ArgCount < Sig->ParamCount ? E->As.Call.ArgCount : Sig->ParamCount;
            for (size_t i = 0; i < CheckCount; i++)
            {
                PRIN_TYPE ArgType = CheckExpr(C, E->As.Call.Args[i]);
                if (!TypesEqual(ArgType, Sig->Params[i].Type) &&
                    !CoerceLiteralTo(E->As.Call.Args[i], Sig->Params[i].Type))
                {
                    ReportError(E->Line, "argument %zu of '%.*s' expects '%s', got '%s'",
                                i + 1, (int)E->As.Call.Length, E->As.Call.Name,
                                TypeName(Sig->Params[i].Type), TypeName(ArgType));
                }
            }

            for (size_t i = CheckCount; i < E->As.Call.ArgCount; i++)
                CheckExpr(C, E->As.Call.Args[i]);

            Result = Sig->ReturnType;
            break;
        }

        default:
            Result = MakeType(C, TY_INT);
            break;
    }

    E->ResolvedType = Result;
    return Result;
}

static void CheckStmt(PCHECKER C, PRIN_STMT S);

static void
CheckBlock(PCHECKER C, PRIN_STMT Block, int NewScope)
{
    if (NewScope)
        PushScope(C);

    for (size_t i = 0; i < Block->As.Block.Count; i++)
        CheckStmt(C, Block->As.Block.Stmts[i]);

    if (NewScope)
        PopScope(C);
}

static void
CheckStmt(PCHECKER C, PRIN_STMT S)
{
    switch (S->Kind)
    {
        case STMT_LET:
        case STMT_MUT:
        {
            PRIN_TYPE DeclType = S->As.VarDecl.Type;

            if (S->As.VarDecl.Init)
            {
                PRIN_TYPE InitType = CheckExpr(C, S->As.VarDecl.Init);
                if (!TypesEqual(DeclType, InitType) && !CoerceLiteralTo(S->As.VarDecl.Init, DeclType))
                {
                    ReportError(S->Line, "cannot initialize '%.*s' of type '%s' with '%s'",
                                (int)S->As.VarDecl.Length, S->As.VarDecl.Name,
                                TypeName(DeclType), TypeName(InitType));
                }
            }

            DeclareSymbol(C, S->As.VarDecl.Name, S->As.VarDecl.Length, DeclType,
                          S->Kind == STMT_MUT, S->Line);
            break;
        }

        case STMT_EXPR:
            CheckExpr(C, S->As.ExprStmt.Expr);
            break;

        case STMT_RET:
        {
            if (S->As.Ret.Value)
            {
                PRIN_TYPE ValType = CheckExpr(C, S->As.Ret.Value);
                if (!TypesEqual(ValType, C->CurrentReturnType))
                {
                    ReportError(S->Line, "return type mismatch: function returns '%s', got '%s'",
                                TypeName(C->CurrentReturnType), TypeName(ValType));
                }
            }
            else if (C->CurrentReturnType->Kind != TY_VOID)
            {
                ReportError(S->Line, "missing return value for function returning '%s'",
                            TypeName(C->CurrentReturnType));
            }
            break;
        }

        case STMT_IF:
        {
            PRIN_TYPE CondType = CheckExpr(C, S->As.If.Cond);
            if (CondType->Kind != TY_BOOL)
                ReportError(S->Line, "'if' condition must be 'bool', got '%s'", TypeName(CondType));

            PushScope(C);
            CheckBlock(C, S->As.If.ThenBranch, 0);
            PopScope(C);

            if (S->As.If.ElseBranch)
            {
                PushScope(C);
                if (S->As.If.ElseBranch->Kind == STMT_BLOCK)
                    CheckBlock(C, S->As.If.ElseBranch, 0);
                else
                    CheckStmt(C, S->As.If.ElseBranch); /* else-if chain */
                PopScope(C);
            }
            break;
        }

        case STMT_WHILE:
        {
            PRIN_TYPE CondType = CheckExpr(C, S->As.While.Cond);
            if (CondType->Kind != TY_BOOL)
                ReportError(S->Line, "'while' condition must be 'bool', got '%s'", TypeName(CondType));

            PushScope(C);
            CheckBlock(C, S->As.While.Body, 0);
            PopScope(C);
            break;
        }

        case STMT_FOR:
        {
            PushScope(C);

            if (S->As.For.Init)
                CheckStmt(C, S->As.For.Init);

            if (S->As.For.Cond)
            {
                PRIN_TYPE CondType = CheckExpr(C, S->As.For.Cond);
                if (CondType->Kind != TY_BOOL)
                    ReportError(S->Line, "'for' condition must be 'bool', got '%s'", TypeName(CondType));
            }

            if (S->As.For.Post)
                CheckExpr(C, S->As.For.Post);

            CheckBlock(C, S->As.For.Body, 0);

            PopScope(C);
            break;
        }

        case STMT_BLOCK:
            CheckBlock(C, S, 1);
            break;
    }
}

int
TypecheckModule(PRIN_MODULE Module, PARENA Arena)
{
    CHECKER C;
    C.Arena = Arena;
    C.CurrentScope = NULL;
    C.CurrentReturnType = NULL;

    C.Funcs = ArenaAlloc(Arena, Module->FunctionCount * sizeof(FUNC_SIG));
    C.FuncCount = Module->FunctionCount;

    for (size_t i = 0; i < Module->FunctionCount; i++)
    {
        RIN_FUNCTION *Fn = &Module->Functions[i];

        for (size_t j = 0; j < i; j++)
        {
            if (NamesEqual(C.Funcs[j].Name, C.Funcs[j].Length, Fn->Name, Fn->Length))
                ReportError(Fn->Line, "redefinition of function '%.*s'", (int)Fn->Length, Fn->Name);
        }

        if (IsFmtOrPrintName(Fn->Name, Fn->Length))
            ReportError(Fn->Line, "cannot redefine builtin '%.*s'", (int)Fn->Length, Fn->Name);

        C.Funcs[i].Name = Fn->Name;
        C.Funcs[i].Length = Fn->Length;
        C.Funcs[i].Params = Fn->Params;
        C.Funcs[i].ParamCount = Fn->ParamCount;
        C.Funcs[i].ReturnType = Fn->ReturnType;
        C.Funcs[i].IsVariadic = Fn->IsVariadic;
    }

    for (size_t i = 0; i < Module->FunctionCount; i++)
    {
        RIN_FUNCTION *Fn = &Module->Functions[i];

        if (Fn->IsExtern)
            continue; /* no body to check */

        PushScope(&C);
        C.CurrentReturnType = Fn->ReturnType;

        for (size_t p = 0; p < Fn->ParamCount; p++)
        {
            DeclareSymbol(&C, Fn->Params[p].Name, Fn->Params[p].Length,
                          Fn->Params[p].Type, 0 /* params are immutable */, Fn->Line);
        }

        CheckBlock(&C, Fn->Body, 0);

        PopScope(&C);
    }

    return !HadError();
}