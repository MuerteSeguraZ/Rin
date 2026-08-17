#include "rin/parser.h"
#include "rin/diag.h"
#include <string.h>
#include <stdlib.h>

typedef struct
{
    LEXER Lexer;
    TOKEN Current;
    TOKEN Previous;
    PARENA Arena;
    int Panic;
} PARSER, *PPARSER;

static void
Advance(PPARSER P)
{
    P->Previous = P->Current;
    P->Current = LexerNext(&P->Lexer);
}

static int
Check(PPARSER P, TOKEN_TYPE Type)
{
    return P->Current.Type == Type;
}

static int
Match(PPARSER P, TOKEN_TYPE Type)
{
    if (!Check(P, Type))
        return 0;
    Advance(P);
    return 1;
}

static void
Expect(PPARSER P, TOKEN_TYPE Type, const char *What)
{
    if (Check(P, Type))
    {
        Advance(P);
        return;
    }

    ReportError(P->Current.Line, "expected %s", What);
    P->Panic = 1;
}

static PRIN_EXPR
NewExpr(PPARSER P, EXPR_KIND Kind, int Line)
{
    PRIN_EXPR E = ArenaAlloc(P->Arena, sizeof(RIN_EXPR));
    E->Kind = Kind;
    E->Line = Line;
    E->ResolvedType = NULL;
    return E;
}

static PRIN_STMT
NewStmt(PPARSER P, STMT_KIND Kind, int Line)
{
    PRIN_STMT S = ArenaAlloc(P->Arena, sizeof(RIN_STMT));
    S->Kind = Kind;
    S->Line = Line;
    return S;
}

static PRIN_TYPE
NewType(PPARSER P, TYPE_KIND Kind)
{
    PRIN_TYPE T = ArenaAlloc(P->Arena, sizeof(RIN_TYPE));
    T->Kind = Kind;
    T->PointeeType = NULL;
    T->ElementType = NULL;
    T->Length = 0;
    T->IsSlice = 0;
    return T;
}

static PRIN_TYPE
ParseType(PPARSER P)
{
    TYPE_KIND Kind;

    switch (P->Current.Type)
    {
        case TOK_INT:   Kind = TY_INT; break;
        case TOK_LONG:  Kind = TY_LONG; break;
        case TOK_CHAR:  Kind = TY_CHAR; break;
        case TOK_BOOL:  Kind = TY_BOOL; break;
        case TOK_VOID:  Kind = TY_VOID; break;
        case TOK_FLOAT: Kind = TY_FLOAT; break;
        case TOK_I8:    Kind = TY_I8; break;
        case TOK_I16:   Kind = TY_I16; break;
        case TOK_U8:    Kind = TY_U8; break;
        case TOK_U16:   Kind = TY_U16; break;
        case TOK_U32:   Kind = TY_U32; break;
        case TOK_U64:   Kind = TY_U64; break;
        default:
            ReportError(P->Current.Line, "expected a type");
            P->Panic = 1;
            return NewType(P, TY_VOID);
    }

    Advance(P);
    PRIN_TYPE Base = NewType(P, Kind);

    /* pointer suffix, possibly repeated: int** */
    while (Match(P, TOK_STAR))
    {
        PRIN_TYPE Ptr = NewType(P, TY_POINTER);
        Ptr->PointeeType = Base;
        Base = Ptr;
    }

    if (Match(P, TOK_LBRACKET))
    {
        PRIN_TYPE Arr = NewType(P, TY_ARRAY);
        Arr->ElementType = Base;

        if (Match(P, TOK_RBRACKET))
        {
            Arr->IsSlice = 1;
        }
        else
        {
            if (!Check(P, TOK_INT_LIT))
            {
                ReportError(P->Current.Line, "expected an array size or ']'");
                P->Panic = 1;
            }
            else
            {
                Arr->Length = (size_t)P->Current.Value.IntValue;
                Advance(P);
            }
            Expect(P, TOK_RBRACKET, "']' after array size");
        }

        Base = Arr;
    }

    return Base;
}

static PRIN_EXPR ParseExpr(PPARSER P);
static PRIN_EXPR ParseAssignment(PPARSER P);

static PRIN_EXPR
ParsePrimary(PPARSER P)
{
    int Line = P->Current.Line;

    if (Match(P, TOK_INT_LIT))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_INT_LIT, Line);
        E->As.IntLit = P->Previous.Value.IntValue;
        return E;
    }

    if (Match(P, TOK_FLOAT_LIT))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_FLOAT_LIT, Line);
        E->As.FloatLit = P->Previous.Value.FloatValue;
        return E;
    }

    if (Match(P, TOK_STRING_LIT))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_STRING_LIT, Line);
        E->As.StringLit.Chars = P->Previous.Start;
        E->As.StringLit.Length = P->Previous.Length;
        return E;
    }

    if (Match(P, TOK_CHAR_LIT))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_CHAR_LIT, Line);
        E->As.CharLit = P->Previous.Length > 0 ? P->Previous.Start[0] : '\0';
        return E;
    }

    if (Match(P, TOK_TRUE))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_BOOL_LIT, Line);
        E->As.BoolLit = 1;
        return E;
    }

    if (Match(P, TOK_FALSE))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_BOOL_LIT, Line);
        E->As.BoolLit = 0;
        return E;
    }

    if (Match(P, TOK_IDENT))
    {
        const char *Name = P->Previous.Start;
        size_t Length = P->Previous.Length;

        /* function call */
        if (Match(P, TOK_LPAREN))
        {
            PRIN_EXPR *Args = NULL;
            size_t ArgCount = 0;
            size_t ArgCap = 0;

            if (!Check(P, TOK_RPAREN))
            {
                do
                {
                    PRIN_EXPR Arg = ParseAssignment(P);
                    if (ArgCount == ArgCap)
                    {
                        size_t NewCap = ArgCap == 0 ? 4 : ArgCap * 2;
                        PRIN_EXPR *NewArgs = ArenaAlloc(P->Arena, NewCap * sizeof(PRIN_EXPR));
                        if (Args)
                            memcpy(NewArgs, Args, ArgCount * sizeof(PRIN_EXPR));
                        Args = NewArgs;
                        ArgCap = NewCap;
                    }
                    Args[ArgCount++] = Arg;
                } while (Match(P, TOK_COMMA));
            }

            Expect(P, TOK_RPAREN, "')' after call arguments");

            PRIN_EXPR E = NewExpr(P, EXPR_CALL, Line);
            E->As.Call.Name = Name;
            E->As.Call.Length = Length;
            E->As.Call.Args = Args;
            E->As.Call.ArgCount = ArgCount;
            E->As.Call.IsFmtBuiltin = 0;
            E->As.Call.IsLenBuiltin = 0;
            E->As.Call.FmtSegments = NULL;
            E->As.Call.FmtSegmentCount = 0;
            return E;
        }

        PRIN_EXPR E = NewExpr(P, EXPR_IDENT, Line);
        E->As.Ident.Name = Name;
        E->As.Ident.Length = Length;
        return E;
    }

    if (Match(P, TOK_LBRACKET))
    {
        PRIN_EXPR *Elements = NULL;
        size_t Count = 0;
        size_t Cap = 0;

        if (!Check(P, TOK_RBRACKET))
        {
            do
            {
                PRIN_EXPR El = ParseAssignment(P);
                if (Count == Cap)
                {
                    size_t NewCap = Cap == 0 ? 4 : Cap * 2;
                    PRIN_EXPR *NewElements = ArenaAlloc(P->Arena, NewCap * sizeof(PRIN_EXPR));
                    if (Elements)
                        memcpy(NewElements, Elements, Count * sizeof(PRIN_EXPR));
                    Elements = NewElements;
                    Cap = NewCap;
                }
                Elements[Count++] = El;
            } while (Match(P, TOK_COMMA));
        }

        Expect(P, TOK_RBRACKET, "']' after array literal");

        PRIN_EXPR E = NewExpr(P, EXPR_ARRAY_LIT, Line);
        E->As.ArrayLit.Elements = Elements;
        E->As.ArrayLit.Count = Count;
        return E;
    }

    if (Match(P, TOK_LPAREN))
    {
        PRIN_EXPR E = ParseExpr(P);
        Expect(P, TOK_RPAREN, "')' after expression");
        return E;
    }

    ReportError(Line, "expected an expression");
    P->Panic = 1;
    return NewExpr(P, EXPR_INT_LIT, Line); /* dummy node to keep parsing */
}

static PRIN_EXPR
ParsePostfix(PPARSER P)
{
    PRIN_EXPR E = ParsePrimary(P);

    while (Match(P, TOK_LBRACKET))
    {
        int Line = P->Previous.Line;
        PRIN_EXPR Idx = ParseExpr(P);
        Expect(P, TOK_RBRACKET, "']' after array index");

        PRIN_EXPR Node = NewExpr(P, EXPR_INDEX, Line);
        Node->As.Index.Array = E;
        Node->As.Index.Index = Idx;
        E = Node;
    }

    return E;
}

static PRIN_EXPR
ParseUnary(PPARSER P)
{
    int Line = P->Current.Line;

    if (Match(P, TOK_MINUS))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_UNARY, Line);
        E->As.Unary.Op = UN_NEG;
        E->As.Unary.Operand = ParseUnary(P);
        return E;
    }

    if (Match(P, TOK_NOT))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_UNARY, Line);
        E->As.Unary.Op = UN_NOT;
        E->As.Unary.Operand = ParseUnary(P);
        return E;
    }

    if (Match(P, TOK_TILDE))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_UNARY, Line);
        E->As.Unary.Op = UN_BNOT;
        E->As.Unary.Operand = ParseUnary(P);
        return E;
    }

    if (Match(P, TOK_AMP))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_UNARY, Line);
        E->As.Unary.Op = UN_ADDR;
        E->As.Unary.Operand = ParseUnary(P);
        return E;
    }

    if (Match(P, TOK_STAR))
    {
        PRIN_EXPR E = NewExpr(P, EXPR_UNARY, Line);
        E->As.Unary.Op = UN_DEREF;
        E->As.Unary.Operand = ParseUnary(P);
        return E;
    }

    return ParsePostfix(P);
}

typedef struct
{
    TOKEN_TYPE Tok;
    BINARY_OP Op;
    int Prec;
} BIN_RULE;

/* precedence table, higher binds tighter */
static const BIN_RULE g_BinRules[] =
{
    { TOK_OR_OR,   BIN_OR,   1 },
    { TOK_AND_AND, BIN_AND,  2 },
    { TOK_PIPE,    BIN_BOR,  3 },
    { TOK_CARET,   BIN_BXOR, 4 },
    { TOK_AMP,     BIN_BAND, 5 },
    { TOK_EQ,      BIN_EQ,   6 },
    { TOK_NEQ,     BIN_NEQ,  6 },
    { TOK_LT,      BIN_LT,   7 },
    { TOK_GT,      BIN_GT,   7 },
    { TOK_LE,      BIN_LE,   7 },
    { TOK_GE,      BIN_GE,   7 },
    { TOK_SHL,     BIN_SHL,  8 },
    { TOK_SHR,     BIN_SHR,  8 },
    { TOK_PLUS,    BIN_ADD,  9 },
    { TOK_MINUS,   BIN_SUB,  9 },
    { TOK_STAR,    BIN_MUL,  10 },
    { TOK_SLASH,   BIN_DIV,  10 },
    { TOK_PERCENT, BIN_MOD,  10 },
};
static const size_t g_BinRuleCount = sizeof(g_BinRules) / sizeof(g_BinRules[0]);

static const BIN_RULE *
FindBinRule(TOKEN_TYPE Tok)
{
    for (size_t i = 0; i < g_BinRuleCount; i++)
    {
        if (g_BinRules[i].Tok == Tok)
            return &g_BinRules[i];
    }
    return NULL;
}

static PRIN_EXPR
ParseBinaryRHS(PPARSER P, int MinPrec, PRIN_EXPR Left)
{
    for (;;)
    {
        const BIN_RULE *Rule = FindBinRule(P->Current.Type);
        if (!Rule || Rule->Prec < MinPrec)
            return Left;

        int Line = P->Current.Line;
        Advance(P); /* consume operator */

        PRIN_EXPR Right = ParseUnary(P);

        for (;;)
        {
            const BIN_RULE *NextRule = FindBinRule(P->Current.Type);
            if (!NextRule || NextRule->Prec <= Rule->Prec)
                break;
            Right = ParseBinaryRHS(P, Rule->Prec + 1, Right);
        }

        PRIN_EXPR NewLeft = NewExpr(P, EXPR_BINARY, Line);
        NewLeft->As.Binary.Op = Rule->Op;
        NewLeft->As.Binary.Left = Left;
        NewLeft->As.Binary.Right = Right;
        NewLeft->As.Binary.OperandType = NULL;
        Left = NewLeft;
    }
}

static PRIN_EXPR
ParseBinary(PPARSER P)
{
    PRIN_EXPR Left = ParseUnary(P);
    return ParseBinaryRHS(P, 1, Left);
}

static PRIN_EXPR
ParseAssignment(PPARSER P)
{
    int Line = P->Current.Line;
    PRIN_EXPR Left = ParseBinary(P);

    if (Match(P, TOK_ASSIGN))
    {
        PRIN_EXPR Value = ParseAssignment(P);

        if (Left->Kind != EXPR_IDENT &&
            Left->Kind != EXPR_INDEX &&
            !(Left->Kind == EXPR_UNARY && Left->As.Unary.Op == UN_DEREF))
        {
            ReportError(Line, "invalid assignment target");
        }

        PRIN_EXPR E = NewExpr(P, EXPR_ASSIGN, Line);
        E->As.Assign.Target = Left;
        E->As.Assign.Value = Value;
        return E;
    }

    return Left;
}

static PRIN_EXPR
ParseExpr(PPARSER P)
{
    return ParseAssignment(P);
}

static PRIN_STMT ParseStatement(PPARSER P);

static PRIN_STMT
ParseBlock(PPARSER P)
{
    int Line = P->Current.Line;
    Expect(P, TOK_LBRACE, "'{' to start block");

    PRIN_STMT *Stmts = NULL;
    size_t Count = 0;
    size_t Cap = 0;

    while (!Check(P, TOK_RBRACE) && !Check(P, TOK_EOF))
    {
        PRIN_STMT S = ParseStatement(P);

        if (Count == Cap)
        {
            size_t NewCap = Cap == 0 ? 8 : Cap * 2;
            PRIN_STMT *NewStmts = ArenaAlloc(P->Arena, NewCap * sizeof(PRIN_STMT));
            if (Stmts)
                memcpy(NewStmts, Stmts, Count * sizeof(PRIN_STMT));
            Stmts = NewStmts;
            Cap = NewCap;
        }
        Stmts[Count++] = S;
    }

    Expect(P, TOK_RBRACE, "'}' to close block");

    PRIN_STMT Block = NewStmt(P, STMT_BLOCK, Line);
    Block->As.Block.Stmts = Stmts;
    Block->As.Block.Count = Count;
    return Block;
}

static PRIN_STMT
ParseVarDecl(PPARSER P, int IsMutable)
{
    int Line = P->Current.Line;
    Advance(P); /* consume 'let' or 'mut' */

    Expect(P, TOK_IDENT, "identifier after 'let'/'mut'");
    const char *Name = P->Previous.Start;
    size_t Length = P->Previous.Length;

    PRIN_TYPE Type = ParseType(P);

    PRIN_EXPR Init = NULL;
    if (Match(P, TOK_ASSIGN))
        Init = ParseExpr(P);

    Expect(P, TOK_SEMI, "';' after variable declaration");

    PRIN_STMT S = NewStmt(P, IsMutable ? STMT_MUT : STMT_LET, Line);
    S->As.VarDecl.Name = Name;
    S->As.VarDecl.Length = Length;
    S->As.VarDecl.Type = Type;
    S->As.VarDecl.Init = Init;
    S->As.VarDecl.IsMutable = IsMutable;
    return S;
}

static PRIN_STMT
ParseIf(PPARSER P)
{
    int Line = P->Current.Line;
    Advance(P); /* 'if' */

    Expect(P, TOK_LPAREN, "'(' after 'if'");
    PRIN_EXPR Cond = ParseExpr(P);
    Expect(P, TOK_RPAREN, "')' after condition");

    PRIN_STMT Then = ParseBlock(P);
    PRIN_STMT Else = NULL;

    if (Match(P, TOK_ELSE))
    {
        if (Check(P, TOK_IF))
            Else = ParseIf(P);
        else
            Else = ParseBlock(P);
    }

    PRIN_STMT S = NewStmt(P, STMT_IF, Line);
    S->As.If.Cond = Cond;
    S->As.If.ThenBranch = Then;
    S->As.If.ElseBranch = Else;
    return S;
}

static PRIN_STMT
ParseWhile(PPARSER P)
{
    int Line = P->Current.Line;
    Advance(P); /* 'while' */

    Expect(P, TOK_LPAREN, "'(' after 'while'");
    PRIN_EXPR Cond = ParseExpr(P);
    Expect(P, TOK_RPAREN, "')' after condition");

    PRIN_STMT Body = ParseBlock(P);

    PRIN_STMT S = NewStmt(P, STMT_WHILE, Line);
    S->As.While.Cond = Cond;
    S->As.While.Body = Body;
    return S;
}

static PRIN_STMT
ParseFor(PPARSER P)
{
    int Line = P->Current.Line;
    Advance(P); /* 'for' */

    Expect(P, TOK_LPAREN, "'(' after 'for'");

    PRIN_STMT Init = NULL;
    if (!Check(P, TOK_SEMI))
    {
        if (Check(P, TOK_LET))
            Init = ParseVarDecl(P, 0);
        else if (Check(P, TOK_MUT))
            Init = ParseVarDecl(P, 1);
        else
        {
            int SLine = P->Current.Line;
            PRIN_EXPR E = ParseExpr(P);
            Expect(P, TOK_SEMI, "';' after for-init");
            Init = NewStmt(P, STMT_EXPR, SLine);
            Init->As.ExprStmt.Expr = E;
        }
    }
    else
    {
        Advance(P); /* consume ';' */
    }

    PRIN_EXPR Cond = NULL;
    if (!Check(P, TOK_SEMI))
        Cond = ParseExpr(P);
    Expect(P, TOK_SEMI, "';' after for-condition");

    PRIN_EXPR Post = NULL;
    if (!Check(P, TOK_RPAREN))
        Post = ParseExpr(P);
    Expect(P, TOK_RPAREN, "')' after for-clauses");

    PRIN_STMT Body = ParseBlock(P);

    PRIN_STMT S = NewStmt(P, STMT_FOR, Line);
    S->As.For.Init = Init;
    S->As.For.Cond = Cond;
    S->As.For.Post = Post;
    S->As.For.Body = Body;
    return S;
}

static PRIN_STMT
ParseRet(PPARSER P)
{
    int Line = P->Current.Line;
    Advance(P); /* 'ret' */

    PRIN_EXPR Value = NULL;
    if (!Check(P, TOK_SEMI))
        Value = ParseExpr(P);

    Expect(P, TOK_SEMI, "';' after 'ret'");

    PRIN_STMT S = NewStmt(P, STMT_RET, Line);
    S->As.Ret.Value = Value;
    return S;
}

static PRIN_STMT
ParseRetf(PPARSER P)
{
    int Line = P->Current.Line;
    Advance(P); /* 'retf' */

    Expect(P, TOK_STRING_LIT, "a string literal format after 'retf'");
    const char *FmtChars = P->Previous.Start;
    size_t FmtLength = P->Previous.Length;
    int FmtLine = P->Previous.Line;

    PRIN_EXPR *Args = NULL;
    size_t ArgCount = 0;
    size_t ArgCap = 0;

    while (Match(P, TOK_COMMA))
    {
        PRIN_EXPR Arg = ParseAssignment(P);
        if (ArgCount == ArgCap)
        {
            size_t NewCap = ArgCap == 0 ? 4 : ArgCap * 2;
            PRIN_EXPR *NewArgs = ArenaAlloc(P->Arena, NewCap * sizeof(PRIN_EXPR));
            if (Args)
                memcpy(NewArgs, Args, ArgCount * sizeof(PRIN_EXPR));
            Args = NewArgs;
            ArgCap = NewCap;
        }
        Args[ArgCount++] = Arg;
    }

    Expect(P, TOK_SEMI, "';' after 'retf' arguments");

    PRIN_EXPR *AllArgs = ArenaAlloc(P->Arena, (ArgCount + 1) * sizeof(PRIN_EXPR));
    PRIN_EXPR FmtExpr = NewExpr(P, EXPR_STRING_LIT, FmtLine);
    FmtExpr->As.StringLit.Chars = FmtChars;
    FmtExpr->As.StringLit.Length = FmtLength;
    AllArgs[0] = FmtExpr;
    for (size_t i = 0; i < ArgCount; i++)
        AllArgs[i + 1] = Args[i];

    PRIN_EXPR CallExpr = NewExpr(P, EXPR_CALL, Line);
    CallExpr->As.Call.Name = "fmt";
    CallExpr->As.Call.Length = 3;
    CallExpr->As.Call.Args = AllArgs;
    CallExpr->As.Call.ArgCount = ArgCount + 1;
    CallExpr->As.Call.IsFmtBuiltin = 0;
    CallExpr->As.Call.IsLenBuiltin = 0;
    CallExpr->As.Call.FmtSegments = NULL;
    CallExpr->As.Call.FmtSegmentCount = 0;

    PRIN_STMT S = NewStmt(P, STMT_RET, Line);
    S->As.Ret.Value = CallExpr;
    return S;
}

static PRIN_STMT
ParseStatement(PPARSER P)
{
    if (Check(P, TOK_LET))
        return ParseVarDecl(P, 0);

    if (Check(P, TOK_MUT))
        return ParseVarDecl(P, 1);

    if (Check(P, TOK_IF))
        return ParseIf(P);

    if (Check(P, TOK_WHILE))
        return ParseWhile(P);

    if (Check(P, TOK_FOR))
        return ParseFor(P);

    if (Check(P, TOK_RETF))
        return ParseRetf(P);

    if (Check(P, TOK_RET))
        return ParseRet(P);

    if (Check(P, TOK_LBRACE))
        return ParseBlock(P);

    int Line = P->Current.Line;
    PRIN_EXPR E = ParseExpr(P);
    Expect(P, TOK_SEMI, "';' after expression");

    PRIN_STMT S = NewStmt(P, STMT_EXPR, Line);
    S->As.ExprStmt.Expr = E;
    return S;
}

/* ---- top level ---- */

static RIN_FUNCTION
ParseFunction(PPARSER P)
{
    RIN_FUNCTION Fn;
    memset(&Fn, 0, sizeof(Fn));
    Fn.Line = P->Current.Line;

    int IsExtern = 0;
    if (Match(P, TOK_EXTERN))
        IsExtern = 1;

    Advance(P); /* 'rite' */

    Expect(P, TOK_IDENT, "function name after 'rite'");
    Fn.Name = P->Previous.Start;
    Fn.Length = P->Previous.Length;

    Expect(P, TOK_LPAREN, "'(' after function name");

    RIN_PARAM *Params = NULL;
    size_t ParamCount = 0;
    size_t ParamCap = 0;
    int IsVariadic = 0;

    if (!Check(P, TOK_RPAREN))
    {
        do
        {
            if (Match(P, TOK_ELLIPSIS))
            {
                if (!IsExtern)
                    ReportError(P->Previous.Line, "'...' is only allowed in 'extern' declarations");
                IsVariadic = 1;
                break; /* '...' must be the last parameter */
            }

            Expect(P, TOK_IDENT, "parameter name");
            const char *PName = P->Previous.Start;
            size_t PLength = P->Previous.Length;
            PRIN_TYPE PType = ParseType(P);

            if (ParamCount == ParamCap)
            {
                size_t NewCap = ParamCap == 0 ? 4 : ParamCap * 2;
                RIN_PARAM *NewParams = ArenaAlloc(P->Arena, NewCap * sizeof(RIN_PARAM));
                if (Params)
                    memcpy(NewParams, Params, ParamCount * sizeof(RIN_PARAM));
                Params = NewParams;
                ParamCap = NewCap;
            }

            Params[ParamCount].Name = PName;
            Params[ParamCount].Length = PLength;
            Params[ParamCount].Type = PType;
            ParamCount++;
        } while (Match(P, TOK_COMMA));
    }

    Expect(P, TOK_RPAREN, "')' after parameters");

    Fn.ReturnType = ParseType(P);
    Fn.Params = Params;
    Fn.ParamCount = ParamCount;
    Fn.IsExtern = IsExtern;
    Fn.IsVariadic = IsVariadic;

    if (IsExtern)
    {
        Expect(P, TOK_SEMI, "';' after extern function declaration");
        Fn.Body = NULL;
    }
    else
    {
        Fn.Body = ParseBlock(P);
    }

    return Fn;
}

PRIN_MODULE
ParseModule(const char *Source, size_t Length, PARENA Arena)
{
    PARSER P;
    LexerInit(&P.Lexer, Source, Length);
    P.Arena = Arena;
    P.Panic = 0;

    /* prime the two-token lookahead-ish buffer */
    P.Current = LexerNext(&P.Lexer);

    RIN_FUNCTION *Functions = NULL;
    size_t Count = 0;
    size_t Cap = 0;

    while (!Check(&P, TOK_EOF))
    {
        int LooksLikeExtern = Check(&P, TOK_EXTERN);
        if (!Check(&P, TOK_RITE) && !LooksLikeExtern)
        {
            ReportError(P.Current.Line, "expected 'rite' or 'extern' at top level");
            Advance(&P); /* avoid infinite loop */
            continue;
        }

        RIN_FUNCTION Fn = ParseFunction(&P);

        if (Count == Cap)
        {
            size_t NewCap = Cap == 0 ? 8 : Cap * 2;
            RIN_FUNCTION *NewFns = ArenaAlloc(Arena, NewCap * sizeof(RIN_FUNCTION));
            if (Functions)
                memcpy(NewFns, Functions, Count * sizeof(RIN_FUNCTION));
            Functions = NewFns;
            Cap = NewCap;
        }
        Functions[Count++] = Fn;
    }

    if (HadError())
        return NULL;

    PRIN_MODULE Mod = ArenaAlloc(Arena, sizeof(RIN_MODULE));
    Mod->Functions = Functions;
    Mod->FunctionCount = Count;
    return Mod;
}