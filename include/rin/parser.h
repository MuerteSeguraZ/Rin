#ifndef RIN_PARSER_H
#define RIN_PARSER_H

#include "rin/lexer.h"
#include "rin/ast.h"
#include "rin/arena.h"

PRIN_MODULE ParseModule(const char *Source, size_t Length, PARENA Arena);

#endif /* RIN_PARSER_H */