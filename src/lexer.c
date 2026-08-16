#include "rin/lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

typedef struct
{
    const char *Keyword;
    TOKEN_TYPE Type;
} KEYWORD_ENTRY;

static const KEYWORD_ENTRY g_Keywords[] =
{
    { "rite",   TOK_RITE },
    { "ret",    TOK_RET },
    { "retf",   TOK_RETF },
    { "let",    TOK_LET },
    { "mut",    TOK_MUT },
    { "if",     TOK_IF },
    { "else",   TOK_ELSE },
    { "while",  TOK_WHILE },
    { "for",    TOK_FOR },
    { "struct", TOK_STRUCT },
    { "import", TOK_IMPORT },
    { "extern", TOK_EXTERN },
    { "true",   TOK_TRUE },
    { "false",  TOK_FALSE },
    { "int",    TOK_INT },
    { "long",   TOK_LONG },
    { "char",   TOK_CHAR },
    { "bool",   TOK_BOOL },
    { "void",   TOK_VOID },
    { "float",  TOK_FLOAT },
    { "i8",     TOK_I8 },
    { "i16",    TOK_I16 },
    { "u8",     TOK_U8 },
    { "u16",    TOK_U16 },
    { "u32",    TOK_U32 },
    { "u64",    TOK_U64 },
};

static const size_t g_KeywordCount = sizeof(g_Keywords) / sizeof(g_Keywords[0]);

void
LexerInit(
    PLEXER Lexer,
    const char *Source,
    size_t Length)
{
    Lexer->Source = Source;
    Lexer->Length = Length;
    Lexer->Pos = 0;
    Lexer->Line = 1;
}

static char
Peek(PLEXER Lexer)
{
    if (Lexer->Pos >= Lexer->Length)
        return '\0';
    return Lexer->Source[Lexer->Pos];
}

static char
PeekNext(PLEXER Lexer)
{
    if (Lexer->Pos + 1 >= Lexer->Length)
        return '\0';
    return Lexer->Source[Lexer->Pos + 1];
}

static char
Advance(PLEXER Lexer)
{
    char c = Lexer->Source[Lexer->Pos++];
    if (c == '\n')
        Lexer->Line++;
    return c;
}

static void
SkipWhitespaceAndComments(PLEXER Lexer)
{
    for (;;)
    {
        char c = Peek(Lexer);

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            Advance(Lexer);
            continue;
        }

        /* line comment */
        if (c == '/' && PeekNext(Lexer) == '/')
        {
            while (Peek(Lexer) != '\n' && Peek(Lexer) != '\0')
                Advance(Lexer);
            continue;
        }

        /* block comment */
        if (c == '/' && PeekNext(Lexer) == '*')
        {
            Advance(Lexer);
            Advance(Lexer);
            while (!(Peek(Lexer) == '*' && PeekNext(Lexer) == '/') && Peek(Lexer) != '\0')
                Advance(Lexer);
            if (Peek(Lexer) != '\0')
            {
                Advance(Lexer);
                Advance(Lexer);
            }
            continue;
        }

        break;
    }
}

static TOKEN
MakeToken(PLEXER Lexer, TOKEN_TYPE Type, const char *Start, size_t Length, int Line)
{
    TOKEN Tok;
    Tok.Type = Type;
    Tok.Start = Start;
    Tok.Length = Length;
    Tok.Line = Line;
    Tok.Value.IntValue = 0;
    return Tok;
}

static TOKEN
LexIdentOrKeyword(PLEXER Lexer)
{
    size_t StartPos = Lexer->Pos;
    int Line = Lexer->Line;

    while (isalnum((unsigned char)Peek(Lexer)) || Peek(Lexer) == '_')
        Advance(Lexer);

    size_t Len = Lexer->Pos - StartPos;
    const char *Start = Lexer->Source + StartPos;

    for (size_t i = 0; i < g_KeywordCount; i++)
    {
        size_t KwLen = strlen(g_Keywords[i].Keyword);
        if (KwLen == Len && strncmp(g_Keywords[i].Keyword, Start, Len) == 0)
            return MakeToken(Lexer, g_Keywords[i].Type, Start, Len, Line);
    }

    return MakeToken(Lexer, TOK_IDENT, Start, Len, Line);
}

static TOKEN
LexNumber(PLEXER Lexer)
{
    size_t StartPos = Lexer->Pos;
    int Line = Lexer->Line;
    int IsFloat = 0;

    while (isdigit((unsigned char)Peek(Lexer)))
        Advance(Lexer);

    if (Peek(Lexer) == '.' && isdigit((unsigned char)PeekNext(Lexer)))
    {
        IsFloat = 1;
        Advance(Lexer);
        while (isdigit((unsigned char)Peek(Lexer)))
            Advance(Lexer);
    }

    size_t Len = Lexer->Pos - StartPos;
    const char *Start = Lexer->Source + StartPos;

    TOKEN Tok = MakeToken(Lexer, IsFloat ? TOK_FLOAT_LIT : TOK_INT_LIT, Start, Len, Line);

    char Buf[64];
    size_t CopyLen = Len < sizeof(Buf) - 1 ? Len : sizeof(Buf) - 1;
    memcpy(Buf, Start, CopyLen);
    Buf[CopyLen] = '\0';

    if (IsFloat)
        Tok.Value.FloatValue = strtod(Buf, NULL);
    else
        Tok.Value.IntValue = (long long)strtoull(Buf, NULL, 10);

    return Tok;
}

static TOKEN
LexString(PLEXER Lexer)
{
    int Line = Lexer->Line;
    Advance(Lexer); /* consume opening quote */
    size_t StartPos = Lexer->Pos;

    while (Peek(Lexer) != '"' && Peek(Lexer) != '\0')
    {
        if (Peek(Lexer) == '\\' && PeekNext(Lexer) != '\0')
            Advance(Lexer); /* skip escaped char */
        Advance(Lexer);
    }

    size_t Len = Lexer->Pos - StartPos;
    const char *Start = Lexer->Source + StartPos;

    if (Peek(Lexer) == '"')
        Advance(Lexer); /* consume closing quote */

    return MakeToken(Lexer, TOK_STRING_LIT, Start, Len, Line);
}

static TOKEN
LexChar(PLEXER Lexer)
{
    int Line = Lexer->Line;
    Advance(Lexer); /* consume opening quote */
    size_t StartPos = Lexer->Pos;

    if (Peek(Lexer) == '\\')
        Advance(Lexer);
    if (Peek(Lexer) != '\0')
        Advance(Lexer);

    size_t Len = Lexer->Pos - StartPos;
    const char *Start = Lexer->Source + StartPos;

    if (Peek(Lexer) == '\'')
        Advance(Lexer);

    return MakeToken(Lexer, TOK_CHAR_LIT, Start, Len, Line);
}

TOKEN
LexerNext(PLEXER Lexer)
{
    SkipWhitespaceAndComments(Lexer);

    if (Lexer->Pos >= Lexer->Length)
        return MakeToken(Lexer, TOK_EOF, Lexer->Source + Lexer->Pos, 0, Lexer->Line);

    char c = Peek(Lexer);
    int Line = Lexer->Line;

    if (isalpha((unsigned char)c) || c == '_')
        return LexIdentOrKeyword(Lexer);

    if (isdigit((unsigned char)c))
        return LexNumber(Lexer);

    if (c == '"')
        return LexString(Lexer);

    if (c == '\'')
        return LexChar(Lexer);

    const char *Start = Lexer->Source + Lexer->Pos;
    Advance(Lexer);

    switch (c)
    {
        case '+': return MakeToken(Lexer, TOK_PLUS, Start, 1, Line);
        case '-': return MakeToken(Lexer, TOK_MINUS, Start, 1, Line);
        case '*': return MakeToken(Lexer, TOK_STAR, Start, 1, Line);
        case '/': return MakeToken(Lexer, TOK_SLASH, Start, 1, Line);
        case '%': return MakeToken(Lexer, TOK_PERCENT, Start, 1, Line);
        case '(': return MakeToken(Lexer, TOK_LPAREN, Start, 1, Line);
        case ')': return MakeToken(Lexer, TOK_RPAREN, Start, 1, Line);
        case '{': return MakeToken(Lexer, TOK_LBRACE, Start, 1, Line);
        case '}': return MakeToken(Lexer, TOK_RBRACE, Start, 1, Line);
        case '[': return MakeToken(Lexer, TOK_LBRACKET, Start, 1, Line);
        case ']': return MakeToken(Lexer, TOK_RBRACKET, Start, 1, Line);
        case ';': return MakeToken(Lexer, TOK_SEMI, Start, 1, Line);
        case ',': return MakeToken(Lexer, TOK_COMMA, Start, 1, Line);
        case '.':
            if (Peek(Lexer) == '.' && PeekNext(Lexer) == '.')
            {
                Advance(Lexer);
                Advance(Lexer);
                return MakeToken(Lexer, TOK_ELLIPSIS, Start, 3, Line);
            }
            return MakeToken(Lexer, TOK_DOT, Start, 1, Line);
        case '~': return MakeToken(Lexer, TOK_TILDE, Start, 1, Line);
        case '^': return MakeToken(Lexer, TOK_CARET, Start, 1, Line);

        case '=':
            if (Peek(Lexer) == '=') { Advance(Lexer); return MakeToken(Lexer, TOK_EQ, Start, 2, Line); }
            return MakeToken(Lexer, TOK_ASSIGN, Start, 1, Line);

        case '!':
            if (Peek(Lexer) == '=') { Advance(Lexer); return MakeToken(Lexer, TOK_NEQ, Start, 2, Line); }
            return MakeToken(Lexer, TOK_NOT, Start, 1, Line);

        case '<':
            if (Peek(Lexer) == '=') { Advance(Lexer); return MakeToken(Lexer, TOK_LE, Start, 2, Line); }
            if (Peek(Lexer) == '<') { Advance(Lexer); return MakeToken(Lexer, TOK_SHL, Start, 2, Line); }
            return MakeToken(Lexer, TOK_LT, Start, 1, Line);

        case '>':
            if (Peek(Lexer) == '=') { Advance(Lexer); return MakeToken(Lexer, TOK_GE, Start, 2, Line); }
            if (Peek(Lexer) == '>') { Advance(Lexer); return MakeToken(Lexer, TOK_SHR, Start, 2, Line); }
            return MakeToken(Lexer, TOK_GT, Start, 1, Line);

        case '&':
            if (Peek(Lexer) == '&') { Advance(Lexer); return MakeToken(Lexer, TOK_AND_AND, Start, 2, Line); }
            return MakeToken(Lexer, TOK_AMP, Start, 1, Line);

        case '|':
            if (Peek(Lexer) == '|') { Advance(Lexer); return MakeToken(Lexer, TOK_OR_OR, Start, 2, Line); }
            return MakeToken(Lexer, TOK_PIPE, Start, 1, Line);
    }

    return MakeToken(Lexer, TOK_ERROR, Start, 1, Line);
}