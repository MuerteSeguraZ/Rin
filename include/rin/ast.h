#ifndef RIN_AST_H
#define RIN_AST_H

#include <stddef.h>

typedef enum
{
    TY_VOID,
    TY_INT,
    TY_LONG,
    TY_CHAR,
    TY_BOOL,
    TY_FLOAT,
    TY_POINTER,
    TY_I8,
    TY_I16,
    TY_U8,
    TY_U16,
    TY_U32,
    TY_U64,
    TY_ARRAY,
} TYPE_KIND;

typedef struct RIN_TYPE
{
    TYPE_KIND Kind;
    struct RIN_TYPE *PointeeType; /* only used when Kind == TY_POINTER */
    struct RIN_TYPE *ElementType;
    size_t Length;
    int IsSlice;
} RIN_TYPE, *PRIN_TYPE;

/* ---- Expressions ---- */

typedef enum
{
    EXPR_INT_LIT,
    EXPR_FLOAT_LIT,
    EXPR_STRING_LIT,
    EXPR_CHAR_LIT,
    EXPR_BOOL_LIT,
    EXPR_IDENT,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_ASSIGN,
    EXPR_CALL,
    EXPR_ARRAY_LIT,
    EXPR_INDEX,
} EXPR_KIND;

typedef enum
{
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_EQ, BIN_NEQ, BIN_LT, BIN_GT, BIN_LE, BIN_GE,
    BIN_AND, BIN_OR,
    BIN_BAND, BIN_BOR, BIN_BXOR, BIN_SHL, BIN_SHR,
} BINARY_OP;

typedef enum
{
    UN_NEG,     /* -x */
    UN_NOT,     /* !x */
    UN_BNOT,    /* ~x */
    UN_ADDR,    /* &x */
    UN_DEREF,   /* *x */
} UNARY_OP;

typedef struct
{
    int IsLiteral;
    const char *Text; /* valid when IsLiteral */
    size_t Length;
    int DataId;
    int ArgIndex;
    char Spec; /* valid when !IsLiteral: 'd', 's', 'c', 'f' */
} FMT_SEGMENT;

typedef struct RIN_EXPR
{
    EXPR_KIND Kind;
    int Line;
    struct RIN_TYPE *ResolvedType; /* filled in by the typechecker */

    union
    {
        long long IntLit;
        double FloatLit;
        int BoolLit;

        struct
        {
            const char *Chars;
            size_t Length;
            int DataId; /* index into module's string table, filled by codegen's pre-pass */
        } StringLit;

        char CharLit;

        struct
        {
            const char *Name;
            size_t Length;
        } Ident;

        struct
        {
            BINARY_OP Op;
            struct RIN_EXPR *Left;
            struct RIN_EXPR *Right;
            struct RIN_TYPE *OperandType;
        } Binary;

        struct
        {
            UNARY_OP Op;
            struct RIN_EXPR *Operand;
        } Unary;

        struct
        {
            struct RIN_EXPR *Target; /* must be an lvalue (EXPR_IDENT or EXPR_UNARY deref) */
            struct RIN_EXPR *Value;
        } Assign;

        struct
        {
            const char *Name;
            size_t Length;
            struct RIN_EXPR **Args;
            size_t ArgCount;

            int IsFmtBuiltin;
            FMT_SEGMENT *FmtSegments;
            size_t FmtSegmentCount;

            int IsLenBuiltin;
        } Call;

        struct
        {
            struct RIN_EXPR **Elements;
            size_t Count;
        } ArrayLit;

        struct
        {
            struct RIN_EXPR *Array;
            struct RIN_EXPR *Index;
        } Index;
    } As;
} RIN_EXPR, *PRIN_EXPR;

typedef enum
{
    STMT_LET,       /* let x int = expr; (immutable) */
    STMT_MUT,       /* mut x int = expr; (mutable) */
    STMT_EXPR,      /* expr; */
    STMT_RET,       /* ret expr; or ret; */
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BLOCK,
} STMT_KIND;

typedef struct RIN_STMT
{
    STMT_KIND Kind;
    int Line;

    union
    {
        struct
        {
            const char *Name;
            size_t Length;
            PRIN_TYPE Type;
            PRIN_EXPR Init; /* may be NULL */
            int IsMutable;
        } VarDecl;

        struct
        {
            PRIN_EXPR Expr;
        } ExprStmt;

        struct
        {
            PRIN_EXPR Value; /* may be NULL for ret; on void functions */
        } Ret;

        struct
        {
            PRIN_EXPR Cond;
            struct RIN_STMT *ThenBranch;
            struct RIN_STMT *ElseBranch; /* may be NULL */
        } If;

        struct
        {
            PRIN_EXPR Cond;
            struct RIN_STMT *Body;
        } While;

        struct
        {
            struct RIN_STMT *Init;   /* may be NULL, a STMT_LET/STMT_MUT/STMT_EXPR */
            PRIN_EXPR Cond;          /* may be NULL */
            PRIN_EXPR Post;          /* may be NULL */
            struct RIN_STMT *Body;
        } For;

        struct
        {
            struct RIN_STMT **Stmts;
            size_t Count;
        } Block;
    } As;
} RIN_STMT, *PRIN_STMT;

typedef struct
{
    const char *Name;
    size_t Length;
    PRIN_TYPE Type;
} RIN_PARAM;

typedef struct
{
    const char *Name;
    size_t Length;
    RIN_PARAM *Params;
    size_t ParamCount;
    PRIN_TYPE ReturnType;
    PRIN_STMT Body; /* STMT_BLOCK */
    int Line;
    int IsExtern;
    int IsVariadic; /* trailing '...' in an extern declaration */
} RIN_FUNCTION, *PRIN_FUNCTION;

typedef struct
{
    RIN_FUNCTION *Functions;
    size_t FunctionCount;
} RIN_MODULE, *PRIN_MODULE;

#endif /* RIN_AST_H */