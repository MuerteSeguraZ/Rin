#ifndef RIN_LEXER_H
#define RIN_LEXER_H

#include "token.h"

typedef struct
{
    const char *Source;
    size_t Length;
    size_t Pos;
    int Line;
} LEXER, *PLEXER;

void LexerInit(PLEXER Lexer, const char *Source, size_t Length);

/* Pull the next token from the stream */
TOKEN LexerNext(PLEXER Lexer);

#endif /* RIN_LEXER_H */