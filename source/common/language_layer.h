#ifndef LANGUAGE_LAYER_H
#define LANGUAGE_LAYER_H

#include <string.h>
#include <inttypes.h>

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

typedef i8  b8;
typedef i16 b16;
typedef i32 b32;

#define global        static
#define internal      static
#define local_persist static

#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)

///////////////////////////////////////////////////////////////////////////////
// string_utf8
///////////////////////////////////////////////////////////////////////////////

typedef struct string_utf8 {
  union {
    void* Data;
    u8*   Str;
  };
  u64 Length;
} string_utf8;

typedef struct utf8_iterator {
  u8* At;
  u8* Stop;
} utf8_iterator;

string_utf8 StringUTF8(char* Data)
{
  string_utf8 Result = {
    .Str    = (u8*)Data,
    .Length = strlen(Data)
  };

  return(Result);
}

utf8_iterator UTF8Iterator(string_utf8 String)
{
  utf8_iterator Result = {
    .At   = String.Str,
    .Stop = String.Str + String.Length
  };

  return(Result);
}

bool IsValid(utf8_iterator Iter)
{
  return(Iter.At < Iter.Stop);
}

u16 UTF8CodepointLengthBytes(utf8_iterator Iter)
{
  char Ch = *Iter.At;

  if (((Ch >> 7) & 0x1) == 0) {
    return(1);
  } else if (((Ch >> 5) & 0x7) == 0x6) {
    return(2);
  } else if (((Ch >> 4) & 0xF) == 0xE) {
    return(3);
  }

  return(4);
}

string_utf8 NextChar(utf8_iterator Iter) {
  string_utf8 Result = {
    .Str    = Iter.At,
    .Length = UTF8CodepointLengthBytes(Iter)
  };

  return(Result);
}

#endif // LANGUAGE_LAYER_H
