#ifndef COMMON_MEMORY_ARENA_H
#define COMMON_MEMORY_ARENA_H

#include "language_layer.h"

typedef struct memory_arena {
  u8  *Base;
  umm Size;
  umm Used;

  u32 ID;
  u32 NumChildren;
  u32 TempCount;

  struct memory_arena *Parent;
} memory_arena;

typedef struct temporary_arena {
  memory_arena *Arena;
  umm SavedUsed;
} temporary_arena;

// Useful macros
#define ArenaPushStruct(Arena_, Type_) (Type_*)ArenaAlloc(Arena_, sizeof(Type_))
#define ArenaPushArray(Arena_, Count_, Type_) (Type_*)ArenaAlloc(Arena_, sizeof(Type_) * Count_)

// ArenaInit initializes a new memory arena from a chunk of memory.
memory_arena ArenaInit(u8 *Memory, umm SizeBytes)
{
  memory_arena Result = {};

  Result.Base = (u8*)Memory;
  Result.Size = SizeBytes;
  Result.Used = 0;
  Result.Parent = NULL;
  Result.ID = 0;
  Result.NumChildren = 0;
  Result.TempCount = 0;

  return(Result);
}

// ArenaClear zeros the memory in the given arena.
void ArenaClear(memory_arena *Arena)
{
  Arena->Used = 0;
  ZeroMemory(Arena->Base, Arena->Size);
}

// ArenaAlloc allocates a chunk of memory of Size bytes from Arena.
u8* ArenaAlloc(memory_arena *Arena, umm Size)
{
  umm TotalSize = Arena->Used + Size;
  assert(TotalSize <= Arena->Size);

  u8 *Result = Arena->Base + Arena->Used;
  Arena->Used += Size;

  return(Result);
}

// ArenaCopy allocates space and copies the given data into the arena
void ArenaCopy(memory_arena *Arena, u8 *Data, umm Size)
{
  u8 *DataPointer = ArenaAlloc(Arena, Size);
  MemoryCopy(DataPointer, Data, Size);
}

// ArenaFree frees a chunk of memory of the given size from the tail of the given arena.
void ArenaFree(memory_arena *Arena, umm Size)
{
  Assert(Arena->Used >= Size);
  Arena->Used -= Size;
}

///////////////////////////////////////////////////////////////////////////////
// child arenas

// ArenaPushChild pushes a child memory arena of Size bytes onto the Parent arena.
memory_arena ArenaPushChild(memory_arena *Parent, umm Size)
{
  memory_arena Result = {};

  Result.Base = ArenaAlloc(Parent, Size);
  Result.Size = Size;
  Result.Used = 0;
  Result.Parent = Parent;
  Result.ID = Parent->NumChildren;
  Result.NumChildren = 0;

  Parent->NumChildren++;

  return(Result);
}

// ArenaPop removes the child arena from its parent, restoring the space it occupied.
void ArenaPopChild(memory_arena *Child)
{
  memory_arena *Parent = Child->Parent;

  assert(Parent);
  assert((Parent->NumChildren - 1) == Child->ID);

  Parent->Used -= Child->Size;

  // Zero out all used memory for later allocations
  ZeroMemory(Parent->Base + Parent->Used, Child->Used);

  Child->Parent = NULL;
  Child->ID = 0;
  Child->Used = 0;

  --Parent->NumChildren;
}

///////////////////////////////////////////////////////////////////////////////
// temporary arenas

// BeginTemporaryArena begins a temporary workd arena in the given memory arena.
temporary_arena BeginTemporaryArena(memory_arena *Arena) {
  temporary_arena Result = {};

  Result.Arena = Arena;
  Result.SavedUsed = Arena->Used;

  Arena->TempCount++;

  return(Result);
}

// EndTemporaryArena removes a temporary arena restoring the space it used.
void EndTemporaryArena(temporary_arena TempArena) {
  memory_arena* Arena = TempArena.Arena;

  // Zero the memory used by the temporary arena
  ZeroMemory(Arena->Base + TempArena.SavedUsed, Arena->Used - TempArena.SavedUsed);

  Arena->Used = TempArena.SavedUsed;

  assert(Arena->TempCount > 0);
  --Arena->TempCount;
}

///////////////////////////////////////////////////////////////////////////////
// scoped arena

typedef struct scoped_arena {
  temporary_arena TempArena;

  scoped_arena(memory_arena *Arena) {
    TempArena = BeginTemporaryArena(Arena);
  }

  ~scoped_arena() {
    EndTemporaryArena(TempArena);
  }
} scoped_arena;

#define ScopedArenaPushArray(ScopedArena, Count, Type) ArenaPushArray(ScopedArena->TempArena.Arena, Count, Type)

internal char* ScopedArenaStrdup(scoped_arena *ScopedArena, const char *Value)
{
  char* Data = ScopedArenaPushArray(ScopedArena, strlen(Value), char);
  strncpy(Data, Value, strlen(Value));

  return(Data);
}

#endif // COMMON_MEMORY_ARENA_H
