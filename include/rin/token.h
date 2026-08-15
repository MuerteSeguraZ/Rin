#ifndef RIN_TOKEN_H
#define RIN_TOKEN_H

#include <stddef.h>

typedef enum
{
    /* Special */
    TOK_EOF,
    TOK_ERROR,

    /* Literals */
    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STRING_LIT,
    TOK_CHAR_LIT,
    TOK_IDENT,

    /* Keywords */
    TOK_RITE,
    TOK_RET,
    TOK_LET,
    TOK_MUT,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_STRUCT,
    TOK_IMPORT,
    TOK_EXTERN,
    TOK_TRUE,
    TOK_FALSE,

    /* Types */
    TOK_INT,
    TOK_LONG,
    TOK_CHAR,
    TOK_BOOL,
    TOK_VOID,
    TOK_FLOAT,

    /* Operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_AND_AND,
    TOK_OR_OR,
    TOK_NOT,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE,
    TOK_SHL,
    TOK_SHR,

    /* Punctuation */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,
    TOK_ELLIPSIS,
} TOKEN_TYPE;

typedef struct
{
    TOKEN_TYPE Type;
    const char *Start;   /* points into source buffer, not null-terminated */
    size_t Length;
    int Line;

    /* precomputed literal values */
    union
    {
        long long IntValue;
        double FloatValue;
    } Value;
} TOKEN, *PTOKEN;

#endif /* RIN_TOKEN_H */