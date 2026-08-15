#include "rin/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ARENA_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGN 8

void
ArenaInit(PARENA Arena)
{
    Arena->Head = NULL;
}

static PARENA_BLOCK
NewBlock(size_t MinSize)
{
    size_t Size = MinSize > ARENA_BLOCK_SIZE ? MinSize : ARENA_BLOCK_SIZE;
    PARENA_BLOCK Block = malloc(sizeof(ARENA_BLOCK) + Size);

    if (!Block)
    {
        fprintf(stderr, "*** ERR: out of memory\n");
        exit(1);
    }

    Block->Next = NULL;
    Block->Size = Size;
    Block->Used = 0;
    return Block;
}

void *
ArenaAlloc(PARENA Arena, size_t Size)
{
    Size = (Size + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1);

    if (!Arena->Head || Arena->Head->Used + Size > Arena->Head->Size)
    {
        PARENA_BLOCK Block = NewBlock(Size);
        Block->Next = Arena->Head;
        Arena->Head = Block;
    }

    void *Ptr = Arena->Head->Data + Arena->Head->Used;
    Arena->Head->Used += Size;
    return Ptr;
}

void
ArenaFree(PARENA Arena)
{
    PARENA_BLOCK Block = Arena->Head;
    while (Block)
    {
        PARENA_BLOCK Next = Block->Next;
        free(Block);
        Block = Next;
    }
    Arena->Head = NULL;
}