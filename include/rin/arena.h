#ifndef RIN_ARENA_H
#define RIN_ARENA_H

#include <stddef.h>

typedef struct ARENA_BLOCK
{
    struct ARENA_BLOCK *Next;
    size_t Size;
    size_t Used;
    char Data[1];
} ARENA_BLOCK, *PARENA_BLOCK;

typedef struct
{
    PARENA_BLOCK Head;
} ARENA, *PARENA;

void ArenaInit(PARENA Arena);
void *ArenaAlloc(PARENA Arena, size_t Size);
void ArenaFree(PARENA Arena);

#endif /* RIN_ARENA_H */