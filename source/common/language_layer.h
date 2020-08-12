#ifndef LANGUAGE_LAYER_H
#define LANGUAGE_LAYER_H

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <time.h>

typedef int8_t    i8;
typedef uint8_t   u8;
typedef int16_t   i16;
typedef uint16_t  u16;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef int64_t   i64;
typedef uint64_t  u64;
typedef uintptr_t umm;
typedef float     f32;
typedef double    f64;

typedef i8  b8;
typedef i16 b16;
typedef i32 b32;

#define global        static
#define internal      static
#define local_persist static

#define foreach(I, Count) for(u32 (I) = 0; (I) < (u32)(Count); ++(I))
#define ArrayCount(Array) ((sizeof(Array))/(sizeof((Array)[0])))
#define UNUSED(X) (void)(X)

#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)

#define Assert(X) assert(X)

#define PI 3.14159265358979
// See "Tau Manifesto": https://tauday.com/tau-manifesto
#define TAU (2*(PI))
#define EULERS_NUMBER 2.718281828459045 // AKA, the constant e
#define ONE_OVER_SQRT_TAU 0.398942280402 // 1/√2π, used for gaussian blur
#define GOLDEN_RATIO 1.6180339887 // Phi, golden mean
#define GOLDEN_RATIO_CONJUGATE 0.6180339887 // Inverse of the golden ratio (Same as, GOLDEN_RATIO - 1)
#define F32_MIN FLT_MIN
#define F32_MAX FLT_MAX

#define Min(A, B) (((A) < (B)) ? (A) : (B))
#define Max(A, B) (((A) > (B)) ? (A) : (B))
#define Sqrt sqrt
#define SinF sinf
#define CosF cosf
#define FModF fmodf
#define AbsoluteValue abs
#define MemoryCopy memcpy

#define Align8(Value) (((Value) + 7) & ~7)

u32 NextPowerOf2(u32 value)
{
    if(value > 0)
    {
      value--;
      value |= value>>1;
      value |= value>>2;
      value |= value>>4;
      value |= value>>8;
      value |= value>>16;
      return value+1;
    }

    return value;
}

void ZeroMemory(u8 *Memory, umm SizeBytes)
{
  memset(Memory, 0, SizeBytes);
}

#define ClearMemory(Value) ZeroMemory((u8*)&Value, sizeof(Value));

internal inline u32 SafeTruncateUInt64(u64 Value)
{
  Assert(Value <= 0xFFFFFFFF);
  return (u32)Value;
}

internal inline f32 RadiansToDegrees(f32 AngleRadians) {
  return AngleRadians * (180.0f / PI);
}

internal inline f32 DegreesToRadians(f32 AngleDegrees) {
  return AngleDegrees * (PI / 180.0f);
}

internal inline f32 Clamp(f32 Value, f32 Min, f32 Max) {
  f32 Result = Value;
  
  if (Value > Max) {
    Result = Max;
  } else if (Value < Min) {
    Result = Min;
  }
  
  return(Result);
}

internal inline f32 Clamp01(f32 Value) {
  f32 Result = Clamp(Value, 0.0f, 1.0f);
  return(Result);
}

internal inline f32 Round(f32 Value) {
  return round(Value);
}

internal inline f32 Truncate(f32 Value) {
  return trunc(Value);
}

internal inline f32 Lerp(f32 Start, f32 End, f32 Weight) {
  f32 Result = (1.0f - Weight)*Start + Weight*End;
  return(Result);
}

inline f32 CosLerp(f32 Start, f32 Stop, f32 Weight)
{
  f32 CosWeight = (1.0f - cosf(Weight * PI)) / 2.0f;
  return Start*(1.0f - CosWeight) + Stop * CosWeight;
}

inline f32 EaseInQuint(f32 Start, f32 Stop, f32 Weight)
{
  f32 Quint = Weight * Weight * Weight * Weight * Weight;
  return Start*(1.0f - Quint) + Stop*Quint;
}

// SeedRandomNumberGenerator seeds the system random number generator with the
// current time and returns the random seed used.
internal u32 SeedRandomNumberGenerator()
{
  u32 Seed = (u32)time(NULL);
  srand(Seed);
  
  return(Seed);
}

i32 RandomI32()
{
  return rand();
}

internal f32 Random01()
{
  return ((f32)RandomI32()/(f32)RAND_MAX);
}

internal f32 Random0To(f32 High)
{
  return Random01() * High;
}

internal f32 RandomRange(f32 Low, f32 High)
{
  return Low + (Random01() * (High - Low));
}

internal i32 RandomI32Range(i32 Low, i32 High)
{
  return(Low + (Random01() * (High - Low)));
}

///////////////////////////////////////////////////////////////////////////////
// stacks
///////////////////////////////////////////////////////////////////////////////

#define Stack(Type, Size) struct { i32 Index; Type Items[Size]; }

#define StackPeek(Stack, Default) ((Stack).Index > 0) ? (Stack).Items[(Stack).Index - 1] : (Default)

#define StackPush(Stack, Value) do {                                 \
    Assert(Stack.Index < (i32)ArrayCount((Stack).Items));            \
    (Stack).Items[(Stack).Index] = (Value);                          \
    (Stack).Index++;                                                 \
  } while(0)

#define StackPop(Stack) do {                    \
    Assert((Stack).Index > 0);                  \
    (Stack).Index--;                            \
  } while(0)

///////////////////////////////////////////////////////////////////////////////
// fnv-1a hash
///////////////////////////////////////////////////////////////////////////////

// 32-bit fnv-1a hash offset basis and prime
#define FNV1A_HASH_INITIAL 2166136261
#define FNV1A_HASH_PRIME 16777619

// fnv-1a hashing algorithm.
//
// This algorithm was selected as it is fast, has few collisions, and maintains
// a good pseudo-random distribution relative to other non-cryptographic
// hashes.
//
// References:
// https://softwareengineering.stackexchange.com/questions/49550/which-hashing-algorithm-is-best-for-uniqueness-and-speed
void Hash(u32* Hash, const u8* Data, u32 DataSizeBytes) {
  const u8* Ptr = Data;
  while (DataSizeBytes--) {
    *Hash = (*Hash ^ *Ptr++) * FNV1A_HASH_PRIME;
  }
}

///////////////////////////////////////////////////////////////////////////////
// djb2 hash
///////////////////////////////////////////////////////////////////////////////

internal u32 HashStringWithSeed(const char* Text, u32 Seed)
{
  // NOTE(eric): Uses the djb2 algorithm for hashing
  u32 Hash = Seed;
  i32 Ch;

  char* Next = (char*)Text;
  while ((Ch = *Next++))
  {
    Hash = ((Hash << 5) + Hash) + Ch;
  }

  return(Hash);
}

internal u32 HashString(const char* Text)
{
  // Uses the default djb2 seed
  return HashStringWithSeed(Text, 5381);
}

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

// Return the next character as a UTF-32 unsigned integer value.
//
// Source: https://gist.github.com/antonijn/9009746
u32 NextCharUTF32(utf8_iterator Iter) {
  u32 NumBytes = UTF8CodepointLengthBytes(Iter);
  u32 Index = 0;
  u8 *At = Iter.At;
  
  u8 NextCh = At[Index];
  u8 Mask = 0;
  u32 RemainingUnits = 0;
  if (NextCh & 0x80)
  {
    Mask = 0xE0;
    for (RemainingUnits = 1; (NextCh & Mask) != (Mask << 1); ++RemainingUnits)
    {
      Mask = (Mask >> 1) | 0x80;
    }
  }
  
  u32 Result = NextCh ^ Mask;
  while (RemainingUnits-- > 0)
  {
    Result <<= 6;
    if (Index >= NumBytes)
    {
      Result = 0;
      break;
    }
    Result |= At[Index++] & 0x3F;
  }
  
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////
// v2u
///////////////////////////////////////////////////////////////////////////////

typedef union v2u {
  struct {
    u32 X, Y;
  };
  struct {
    u32 Width, Height;
  };
  
  u32 E[2];
} v2u;

inline v2u V2U(u32 X, u32 Y) {
  v2u Result = { .X = X, .Y = Y };
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////
// v2i
///////////////////////////////////////////////////////////////////////////////

typedef union v2i {
  struct {
    i32 X, Y;
  };
  struct {
    i32 Width, Height;
  };

  i32 E[2];
} v2i;

inline v2i V2I(i32 X, i32 Y) {
  v2i Result = { .X = X, .Y = Y };
  return(Result);
}

inline v2i V2I(v2u V) {
  return V2I(V.X, V.Y);
}

inline v2i operator - (v2i Left, v2i Right) {
  v2i Result = V2I(Left.X - Right.X, Left.Y - Right.Y);
  return(Result);
}

inline v2i operator + (v2i Left, v2i Right) {
  v2i Result = V2I(Left.X + Right.X, Left.Y + Right.Y);
  return(Result);
}

inline v2i operator += (v2i& Left, v2i Right) {
  Left.X += Right.X;
  Left.Y += Right.Y;
  return(Left);
}

///////////////////////////////////////////////////////////////////////////////
// v2
///////////////////////////////////////////////////////////////////////////////

typedef union v2 {
  struct {
    f32 X, Y;
  };
  struct {
    f32 Width, Height;
  };
  
  f32 E[2];
} v2;

inline v2 V2(f32 X, f32 Y) {
  v2 Result = {{X, Y}};
  return(Result);
}

inline v2 V2(v2i V) {
  v2 Result = V2(V.X, V.Y);
  return(Result);
}

inline v2 V2(v2u V) {
  v2 Result = V2(V.X, V.Y);
  return(Result);
}

inline v2 V2(f32 Value) {
  return(V2(Value, Value));
}

inline v2 operator *(v2 Left, f32 Right) {
  v2 Result = {{Left.X*Right, Left.Y*Right}};
  return(Result);
}

inline v2u operator * (v2u Left, v2 Right) {
  v2u Result = V2U(Left.X * Right.X, Left.Y * Right.Y);
  return(Result);
}

inline v2 operator *(f32 Left, v2 Right) {
  v2 Result = {{Left*Right.X, Left*Right.Y}};
  return(Result);
}

inline v2 operator *(v2 Left, v2 Right) {
  v2 Result = {{Left.X * Right.X, Left.Y * Right.Y}};
  return(Result);
}

inline v2& operator *=(v2& Left, f32 Right) {
  Left = Left * Right;
  return(Left);
}

inline v2& operator *=(v2& Left, v2 Right) {
  Left.X *= Right.X;
  Left.Y *= Right.Y;
  return(Left);
}

inline v2 operator / (v2 Left, v2 Right) {
  return(V2(Left.X / Right.X, Left.Y / Right.Y));
}

inline v2& operator /=(v2& Left, v2 Right) {
  Left.X /= Right.X;
  Left.Y /= Right.Y;
  return(Left);
}

inline v2 operator +(v2 Left, v2 Right) {
  v2 Result = {{Left.X+Right.X, Left.Y+Right.Y}};
  return(Result);
}

inline v2 operator +=(v2& Left, v2 Right) {
  Left.X += Right.X;
  Left.Y += Right.Y;
  return(Left);
}

inline v2 operator -(v2 Left, v2 Right) {
  v2 Result = {{Left.X-Right.X, Left.Y-Right.Y}};
  return(Result);
}

inline bool operator ==(v2 Left, v2 Right) {
  return (Left.X == Right.X && Left.Y == Right.Y);
}

inline bool operator !=(v2 Left, v2 Right) {
  return (Left.X != Right.X || Left.Y != Right.Y);
}

inline v2 FloorV2(v2 Vec) {
  v2 Result = V2(floor(Vec.X), floor(Vec.Y));
  return(Result);
}

inline v2 Clamp01(v2 Vec) {
  v2 Result = V2(Clamp01(Vec.X), Clamp01(Vec.Y));
  return(Result);
}

inline v2 Round(v2 Value) {
  v2 Result = V2(round(Value.X), round(Value.Y));
  return(Result);
}

inline v2 Ceiling(v2 Value) {
  v2 Result = V2(ceil(Value.X), ceil(Value.Y));
  return(Result);
}

// Inner a.k.a. Dot product
inline f32 Inner(v2 A, v2 B) {
  return(A.X*B.X + A.Y*B.Y);
}

inline f32 LengthSq(v2 Vec) {
  return(Vec.X*Vec.X + Vec.Y*Vec.Y);
}

inline f32 Length(v2 Vec) {
  return sqrt(LengthSq(Vec));
}

inline v2 NOZ(v2 Vec) {
  f32 Magnitude = Length(Vec);
  if (Magnitude > 0.0f) {
    f32 OneOverMagnitude = 1.0f / Magnitude;
    return Vec * OneOverMagnitude;
  }
  
  return V2(0.0f, 0.0f);
}

inline v2 Lerp(v2 Start, v2 End, f32 Weight) {
  v2 Result = (1.0f - Weight)*Start + Weight*End;
  return(Result);
}

inline f32 AngleRadiansBetween(v2 A, v2 B) {
  v2 NormA = NOZ(A);
  v2 NormB = NOZ(B);
  return(acos(Inner(NormA, NormB)));
}

inline v2 ScreenToClipSpace(v2 ScreenPos, v2 RenderDim) {
  return(V2((2 * ScreenPos.X) / RenderDim.Width - 1,
            (2 * ScreenPos.Y) / RenderDim.Height - 1));
}

///////////////////////////////////////////////////////////////////////////////
// v3
///////////////////////////////////////////////////////////////////////////////

typedef union v3 {
  struct {
    f32 X, Y, Z;
  };
  struct {
    f32 R, G, B;
  };
  struct {
    f32 H, S, V;
  };
  struct {
    v2 XY;
    f32 _Z;
  };
  
  f32 E[3];
} v3;

inline v3 V3(f32 X, f32 Y, f32 Z) {
  v3 Result = {{X, Y, Z}};
  return(Result);
}

inline v3 V3(f32 Value) {
  return(V3(Value, Value, Value));
}

inline v3 V3(v2 Pos, f32 Z) {
  v3 Result = {{Pos.X, Pos.Y, Z}};
  return(Result);
}

inline v3 operator *(v3 Left, f32 Right) {
  v3 Result;
  Result.X = Left.X*Right;
  Result.Y = Left.Y*Right;
  Result.Z = Left.Z*Right;
  return(Result);
}

inline v3 operator *(f32 Left, v3 Right) {
  return(Right*Left);
}

inline v3 operator *(v3 Left, v3 Right) {
  v3 Result;
  Result.X = Left.X*Right.X;
  Result.Y = Left.X*Right.Y;
  Result.Z = Left.X*Right.Z;
  return(Result);
}

inline v3 operator -(v3 Left, v3 Right) {
  v3 Result;
  Result.X = Left.X-Right.X;
  Result.Y = Left.Y-Right.Y;
  Result.Z = Left.Z-Right.Z;
  return(Result);
}

inline v3 operator +(v3 Left, v3 Right) {
  v3 Result;
  Result.X = Left.X+Right.X;
  Result.Y = Left.Y+Right.Y;
  Result.Z = Left.Z+Right.Z;
  return(Result);
}

inline v3 operator +=(v3& Left, v3 Right) {
  Left.X += Right.X;
  Left.Y += Right.Y;
  Left.Z += Right.Z;
  return(Left);
}

inline f32 LengthSq(v3 Vec) {
  return(Vec.X*Vec.X + Vec.Y*Vec.Y + Vec.Z*Vec.Z);
}

inline f32 Length(v3 Vec) {
  return Sqrt(LengthSq(Vec));
}

inline v3 NOZ(v3 Vec) {
  f32 Magnitude = Length(Vec);
  if (Magnitude > 0.0f) {
    f32 OneOverMagnitude = 1.0f / Magnitude;
    return Vec * OneOverMagnitude;
  }
  
  return V3(0.0f, 0.0f, 0.0f);
}

inline f32 Inner(v3 A, v3 B) {
  f32 Result = A.X*B.X + A.Y*B.Y + A.Z*B.Z;
  return(Result);
}

inline v3 Cross(v2 A, v2 B) {
  v3 Result = V3(
                 0,
                 0,
                 A.X*B.Y - A.Y*B.X
                 );
  return(Result);
}

inline v3 Cross(v3 A, v3 B) {
  v3 Result = {{
      (A.Y*B.Z - A.Z*B.Y),
      (A.Z*B.X - A.X*B.Z),
      (A.X*B.Y - A.Y*B.X)
    }};
  return(Result);
}

inline v3 Lerp(v3 Start, v3 End, f32 Weight) {
  v3 Result = (1.0f - Weight)*Start + Weight*End;
  return(Result);
}

inline v3 FloorV3(v3 Vec) {
  v3 Result = V3(floor(Vec.X), floor(Vec.Y), floor(Vec.Z));
  return(Result);
}

inline v3 Round(v3 Vec) {
  v3 Result = V3(Round(Vec.X), Round(Vec.Y), Round(Vec.Z));
  return(Result);
}

inline v3 Truncate(v3 Vec) {
  v3 Result = V3(Truncate(Vec.X), Truncate(Vec.Y), Truncate(Vec.Z));
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////
// v4
///////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)
typedef union v4 {
  struct {
    f32 X, Y, Z, W;
  };
  struct {
    v2 XY;
    v2 ZW; 
  };
  struct {
    v3 XYZ;
    f32 _W;
  };
  struct {
    f32 _X, _Y, Width, Height;
  };
  struct {
    f32 R, G, B, A;
  };
  
  f32 E[4];
} v4;
#pragma pack(pop)

inline v4 V4(f32 X, f32 Y, f32 Z, f32 W) {
  v4 Result = {{X, Y, Z, W}};
  return(Result);
}

inline v4 V4(f32 Value) {
  return(V4(Value, Value, Value, Value));
}

inline v4 V4(v2 XY, v2 ZW) {
  return(V4(XY.X, XY.Y, ZW.X, ZW.Y));
}

inline v4 V4(v3 Vec, f32 W) {
  return V4(Vec.X, Vec.Y, Vec.Z, W);
}

inline bool operator !=(v4 Left, v4 Right) {
  return (
          Left.X != Right.X ||
          Left.Y != Right.Y ||
          Left.Width != Right.Width ||
          Left.Height != Right.Height
          );
}

inline v4 operator *(v4 Left, v4 Right) {
  v4 Result = V4(
                 Left.X * Right.X,
                 Left.Y * Right.Y,
                 Left.Z * Right.Z,
                 Left.W * Right.W
                 );
  return(Result);
}

inline v4 operator *(f32 Left, v4 Right) {
  v4 Result = {{
      Left * Right.X,
      Left * Right.Y,
      Left * Right.Z,
      Left * Right.W
    }};
  return(Result);
}

inline v4 operator +(v4 Left, v4 Right) {
  v4 Result = {{
      Left.X + Right.X,
      Left.Y + Right.Y,
      Left.Z + Right.Z,
      Left.W + Right.W
    }};
  return(Result);
}

inline v4 operator -(v4 Left, v4 Right) {
  v4 Result = {{
      Left.X - Right.X,
      Left.Y - Right.Y,
      Left.Z - Right.Z,
      Left.W - Right.W
    }};
  return(Result);
}

inline v4 Round(v4 Vec) {
  v4 Result = V4(Round(Vec.X), Round(Vec.Y), Round(Vec.Z), Round(Vec.W));
  return(Result);
}

inline v4 Ceiling(v4 Vec) {
  v4 Result = V4(ceil(Vec.X), ceil(Vec.Y), ceil(Vec.Z), ceil(Vec.W));
  return(Result);
}

inline v4 FloorV4(v4 Vec) {
  v4 Result = V4(floor(Vec.X), floor(Vec.Y), floor(Vec.Z), floor(Vec.W));
  return(Result);
}

inline v4 Lerp(v4 Start, v4 End, f32 Weight) {
  v4 Result = (1.0f - Weight)*Start + Weight*End;
  return(Result);
}

// Expands the rectangle by the given amount. Negative amounts contract the
// rectangle.
inline v4 ExpandRect(v4 Rect, f32 ExpandAmount) {
  Rect.X -= ExpandAmount;
  Rect.Y -= ExpandAmount;
  Rect.Width += 2 * ExpandAmount;
  Rect.Height += 2 * ExpandAmount;
  return(Rect);
}

// Returns the rectangular intersection of A and B.
inline v4 IntersectRects(v4 A, v4 B) {
  f32 X1 = Max(A.X, B.X);
  f32 Y1 = Max(A.Y, B.Y);
  f32 X2 = Min(A.X + A.Width, B.X + B.Width);
  f32 Y2 = Min(A.Y + A.Height, B.Y + B.Height);
  if (X2 < X1) { X2 = X1; }
  if (Y2 < Y1) { Y2 = Y1; }
  return V4(X1, Y1, X2 - X1, Y2 - Y1);
}

///////////////////////////////////////////////////////////////////////////////
// m4x4
///////////////////////////////////////////////////////////////////////////////

// NOTE: All matrices are in ROW-MAJOR format. This means that all
// multiplication with column vectors must be post-multiplication. That is, for
// a given vector V if you want to apply matrix transformations A, B, and C you
// must compute V' = V x A x B x C and NOT the column-major pre-multiplication
// V' = C x B x A x V.
typedef struct {
  f32 E[4][4];
} m4x4;

inline m4x4 Identity4x4() {
  m4x4 Result = {{
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1}
    }};
  
  return(Result);
}

inline b32 operator ==(m4x4 Left, m4x4 Right) {
  b32 Result = true;
  
  for (u32 I = 0; I < 4; ++I) {
    for (u32 J = 0; J < 4; ++J) {
      if (fabs(Left.E[I][J] - Right.E[I][J]) > 0.0005) {
        //if (Left.E[I][J] != Right.E[I][J]) {
        printf("Mismatch %d %d (%f != %f)\n", I, J, Left.E[I][J], Right.E[I][J]);
        Result = false;
      }
    }
  }
  
  return(Result);
}

inline m4x4 operator *(m4x4 Left, m4x4 Right) {
  m4x4 Result;
  
  for (u32 I = 0; I < 4; ++I) {
    for (u32 J = 0; J < 4; ++J) {
      Result.E[I][J] = 0.0f;
      for (u32 K = 0; K < 4; ++K) {
        Result.E[I][J] += Left.E[I][K]*Right.E[K][J];
      }
    }
  }
  
  return(Result);
}

// Multiply the row vector Left by the matrix Right.
inline v4 operator *(v4 Left, m4x4 Right) {
  v4 Result = {};
  
  for (u32 I = 0; I < 4; ++I)
  {
    for (u32 J = 0; J < 4; ++J)
    {
      // NOTE: Left multiplying a matrix by a row vector computes the each new
      // element V'_I of an n-element row vector by multiplying the input
      // vector V by row R_I from the matrix.
      Result.E[I] += Left.E[J] * Right.E[I][J];
    }
  }
  
  return(Result);
}

// Multiply the column vector Right by the matrix Left.
inline v4 operator *(m4x4 Left, v4 Right) {
  v4 Result = {};
  
  for (u32 Column = 0; Column < 4; ++Column)
  {
    for (u32 Row = 0; Row < 4; ++Row)
    {
      Result.E[Column] += Left.E[Row][Column] * Right.E[Row];
    }
  }
  
  return(Result);
}

inline v3 operator * (v3 Left, m4x4 Right) {
  v4 Result = V4(Left.X, Left.Y, Left.Z, 1) * Right;
  return Result.XYZ;
}

inline v3 operator * (m4x4 Left, v3 Right) {
  v4 Result = Left * V4(Right.X, Right.Y, Right.Z, 1);
  return Result.XYZ;
}

// Multiply the column vector Right by the matrix Left.
inline v2 operator *(m4x4 Left, v2 Right) {
  v4 Equiv = V4(Right.X, Right.Y, 0, 1);
  v4 Result = Left * Equiv;
  return Result.XY;
}

// Multiply the row vector Left by the matrix Right.
inline v2 operator *(v2 Left, m4x4 Right) {
  v4 Equiv = V4(Left.X, Left.Y, 0, 1);
  v4 Result = Equiv * Right;
  return Result.XY;
}

///////////////////////////////////////////////////////////////////////////////
// colors
///////////////////////////////////////////////////////////////////////////////

// NOTE(eric): Results HSV where all values are in [0, 1].
inline v3 RGBToHSV(v3 RGB)
{
  v3 Result = {};
  
  f32 MinC = Min(RGB.R, Min(RGB.G, RGB.B));
  f32 MaxC = Max(RGB.R, Max(RGB.G, RGB.B));
  f32 Delta = MaxC - MinC;
  
  if (Delta < 0.0001)
  {
    Result.S = 0;
    Result.H = 0;
    Result.V = MaxC;
    return(Result);
  }
  
  // NOTE(eric): Hue (H)
  if (RGB.R >= MaxC)
  {
    Result.H = (RGB.G - RGB.B) / Delta;
  }
  else if (RGB.G >= MaxC)
  {
    Result.H = (2 + (RGB.B - RGB.R) / Delta);
  }
  else if (RGB.B >= MaxC)
  {
    Result.H = (4 + (RGB.R - RGB.G) / Delta);
  }
  else
  {
    Result.H = 0;
  }
  
  if (Result.H < 0)
  {
    Result.H += 6.0f;
  }
  
  Result.H /= 6.0f;
  
  // NOTE(eric): Saturation (S)
  if (MaxC == 0)
  {
    Result.S = 0;
  }
  else
  {
    Result.S = Delta / MaxC;
  }
  
  // NOTE(eric): Value (V)
  Result.V = MaxC;
  
  return(Result);
}

// NOTE(eric): Returns RGB where all valus are in [0, 1].
inline v3 HSVToRGB(v3 HSV)
{
  v3 Result = {};
  
  f32 Hue = FModF(HSV.H * 360.0f, 360.0f);
  f32 Sat = HSV.S;
  f32 Val = HSV.V;
  
  f32 C = Val * Sat;
  f32 X = C * (1.0f - AbsoluteValue(FModF((Hue / 60.0f), 2) - 1.0f));
  f32 M = Val - C;
  
  if ((Hue >= 0.0f && Hue < 60.0f) || (Hue >= 360.0f && Hue < 420.0f))
  {
    Result.R = C;
    Result.G = X;
    Result.B = 0;
  }
  else if (Hue >= 60.0f && Hue < 120.0f)
  {
    Result.R = X;
    Result.G = C;
    Result.B = 0;
  }
  else if (Hue >= 120.0f && Hue < 180.0f)
  {
    Result.R = 0;
    Result.G = C;
    Result.B = X;
  }
  else if (Hue >= 180.0f && Hue < 240.0f)
  {
    Result.R = 0;
    Result.G = X;
    Result.B = C;
  }
  else if (Hue >= 240.0f && Hue < 300.0f)
  {
    Result.R = X;
    Result.G = 0;
    Result.B = C;
  }
  else if ((Hue >= 300.0f && Hue <= 360.0f) || (Hue >= -60.0f && Hue <= 0.0f))
  {
    Result.R = C;
    Result.G = 0;
    Result.B = X;
  }
  
  Result += V3(M, M, M);
  return(Result);
}

// Maps a point from one resolution to another
inline v2 MapPointToResolution(v2 Point, v2 FromResolution, v2 ToResolution)
{
  v2 ClipSpacePoint = V2(
    Point.X / FromResolution.Width,
    Point.Y / FromResolution.Height
  );
  v2 Result = V2(
    ClipSpacePoint.X * ToResolution.Width,
    ClipSpacePoint.Y * ToResolution.Height
  );
  return FloorV2(Result);
}

// Maps a rectangle from one resolution to another
inline v4 MapRectToResolution(v4 Rect, v2 FromResolution, v2 ToResolution)
{
  v4 ClipSpaceRect = V4(
    Rect.X / FromResolution.Width,
    Rect.Y / FromResolution.Height,
    Rect.Width / FromResolution.Width,
    Rect.Height / FromResolution.Height
  );
  v4 Result = V4(
    ClipSpaceRect.X * ToResolution.Width,
    ClipSpaceRect.Y * ToResolution.Height,
    ClipSpaceRect.Width * ToResolution.Width,
    ClipSpaceRect.Height * ToResolution.Height
  );
  // Because we're using a coordinate system with (0, 0) at the lower left-hand
  // corner we need to floor the coordinates to avoid and over-small start
  // position while taking the ceiling of the width and height values to ensure
  // that we always have sufficient space for the rectangle even if we end up
  // using slightly more screen real-estate.
  return V4(round(Result.X), round(Result.Y), ceil(Result.Width), ceil(Result.Height));
}

///////////////////////////////////////////////////////////////////////////////
// m4x4
///////////////////////////////////////////////////////////////////////////////

inline m4x4 Tranpose(m4x4 Matrix)
{
  m4x4 Result = {};
  
  for (u32 Y = 0; Y < 4; ++Y)
  {
    for (u32 X = 0; X < 4; ++X)
    {
      Result.E[Y][X] = Matrix.E[X][Y];
    }
  }
  
  return(Result);
}

inline m4x4 Inverse(m4x4 Matrix)
{
  m4x4 Result = {};
  f32 Det = 0.0f;
  
  f32* M = (f32*)Matrix.E;
  f32* Inv = (f32*)Result.E;
  
  Inv[0] = (
            M[5] * M[10] * M[15] -
            M[5] * M[11] * M[14] -
            M[9] * M[6] * M[15] +
            M[9] * M[7] * M[14] +
            M[13] * M[6] * M[11] -
            M[13] * M[7] * M[10]
            );
  
  Inv[4] = (
            -M[4]  * M[10] * M[15] +
            M[4]  * M[11] * M[14] +
            M[8]  * M[6]  * M[15] -
            M[8]  * M[7]  * M[14] -
            M[12] * M[6]  * M[11] +
            M[12] * M[7]  * M[10]
            );
  
  Inv[8] = (
            M[4]  * M[9] * M[15] -
            M[4]  * M[11] * M[13] -
            M[8]  * M[5] * M[15] +
            M[8]  * M[7] * M[13] +
            M[12] * M[5] * M[11] -
            M[12] * M[7] * M[9]
            );
  
  Inv[12] = (
             -M[4]  * M[9] * M[14] +
             M[4]  * M[10] * M[13] +
             M[8]  * M[5] * M[14] -
             M[8]  * M[6] * M[13] -
             M[12] * M[5] * M[10] +
             M[12] * M[6] * M[9]
             );
  
  Inv[1] = (
            -M[1]  * M[10] * M[15] +
            M[1]  * M[11] * M[14] +
            M[9]  * M[2] * M[15] -
            M[9]  * M[3] * M[14] -
            M[13] * M[2] * M[11] +
            M[13] * M[3] * M[10]
            );
  
  Inv[5] = (
            M[0]  * M[10] * M[15] -
            M[0]  * M[11] * M[14] -
            M[8]  * M[2] * M[15] +
            M[8]  * M[3] * M[14] +
            M[12] * M[2] * M[11] -
            M[12] * M[3] * M[10]
            );
  
  Inv[9] = (
            -M[0]  * M[9] * M[15] +
            M[0]  * M[11] * M[13] +
            M[8]  * M[1] * M[15] -
            M[8]  * M[3] * M[13] -
            M[12] * M[1] * M[11] +
            M[12] * M[3] * M[9]
            );
  
  Inv[13] = (
             M[0]  * M[9] * M[14] -
             M[0]  * M[10] * M[13] -
             M[8]  * M[1] * M[14] +
             M[8]  * M[2] * M[13] +
             M[12] * M[1] * M[10] -
             M[12] * M[2] * M[9]
             );
  
  Inv[2] = (
            M[1]  * M[6] * M[15] -
            M[1]  * M[7] * M[14] -
            M[5]  * M[2] * M[15] +
            M[5]  * M[3] * M[14] +
            M[13] * M[2] * M[7] -
            M[13] * M[3] * M[6]
            );
  
  Inv[6] = (
            -M[0]  * M[6] * M[15] +
            M[0]  * M[7] * M[14] +
            M[4]  * M[2] * M[15] -
            M[4]  * M[3] * M[14] -
            M[12] * M[2] * M[7] +
            M[12] * M[3] * M[6]
            );
  
  Inv[10] = (
             M[0]  * M[5] * M[15] -
             M[0]  * M[7] * M[13] -
             M[4]  * M[1] * M[15] +
             M[4]  * M[3] * M[13] +
             M[12] * M[1] * M[7] -
             M[12] * M[3] * M[5]
             );
  
  Inv[14] = (
             -M[0]  * M[5] * M[14] +
             M[0]  * M[6] * M[13] +
             M[4]  * M[1] * M[14] -
             M[4]  * M[2] * M[13] -
             M[12] * M[1] * M[6] +
             M[12] * M[2] * M[5]
             );
  
  Inv[3] = (
            -M[1] * M[6] * M[11] +
            M[1] * M[7] * M[10] +
            M[5] * M[2] * M[11] -
            M[5] * M[3] * M[10] -
            M[9] * M[2] * M[7] +
            M[9] * M[3] * M[6]
            );
  
  Inv[7] = (
            M[0] * M[6] * M[11] -
            M[0] * M[7] * M[10] -
            M[4] * M[2] * M[11] +
            M[4] * M[3] * M[10] +
            M[8] * M[2] * M[7] -
            M[8] * M[3] * M[6]
            );
  
  Inv[11] = (
             -M[0] * M[5] * M[11] +
             M[0] * M[7] * M[9] +
             M[4] * M[1] * M[11] -
             M[4] * M[3] * M[9] -
             M[8] * M[1] * M[7] +
             M[8] * M[3] * M[5]
             );
  
  Inv[15] = (
             M[0] * M[5] * M[10] -
             M[0] * M[6] * M[9] -
             M[4] * M[1] * M[10] +
             M[4] * M[2] * M[9] +
             M[8] * M[1] * M[6] -
             M[8] * M[2] * M[5]
             );
  
  Det = M[0] * Inv[0] + M[1] * Inv[4] + M[2] * Inv[8] + M[3] * Inv[12];
  
  // Trigger an error if we try to invert a non-invertable matrix
  Assert(Det != 0);
  
  Det = 1.0 / Det;
  
  for (u32 I = 0; I < 16; I++)
  {
    Inv[I] *= Det;
  }
  
#if 0
  m4x4 Test = Result * Matrix;
  if (!(Test == Identity4x4()))
  {
    for (u32 I = 0; I < 4; ++I)
    {
      for (u32 J = 0; J < 4; ++J)
      {
        printf("%f, ", Test.E[I][J]);
      }
      printf("\n");
    }
    Assert(Test == Identity4x4());
  }
#endif
  
  return(Result);
}

inline m4x4 Transpose(m4x4 M) {
  m4x4 Result = {};
  
  for (u32 I = 0; I < 4; ++I)
  {
    for (u32 J = 0; J < 4; ++J)
    {
      Result.E[J][I] = M.E[I][J];
    }
  }
  
  return(Result);
}

inline m4x4 Orthographic(f32 Left, f32 Right, f32 Bottom, f32 Top, f32 Near, f32 Far) {
  f32 L = Left;
  f32 R = Right;
  f32 T = Top;
  f32 B = Bottom;
  f32 N = Near;
  f32 F = Far;
  
  m4x4 Result = {{
      { 2.0f / (R - L), 0             , 0              , -(R + L) / (R - L) },
      { 0             , 2.0f / (T - B), 0              , -(T + B) / (T - B) },
      { 0             , 0             , -2.0f / (F - N), -(F + N) / (F - N) },
      { 0             , 0             , 0              , 1 }
    }};
  
  return Result;
}

inline m4x4 TranslationMatrix(f32 X, f32 Y, f32 Z) {
  m4x4 Result = {{
      { 1, 0, 0, X },
      { 0, 1, 0, Y },
      { 0, 0, 1, Z },
      { 0, 0, 0, 1 }
    }};
  
  return Result;
}

inline m4x4 RotationMatrixZ(f32 AngleRadians) {
  m4x4 Result = {{
      {cosf(AngleRadians), -sinf(AngleRadians), 0, 0},
      {sinf(AngleRadians), cosf(AngleRadians), 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1}
    }};
  
  return(Result);
}

inline m4x4 ScalingMatrix(f32 XScale, f32 YScale, f32 ZScale) {
  m4x4 Result = {{
      { XScale, 0, 0, 0 },
      { 0, YScale, 0, 0 },
      { 0, 0, ZScale, 0 },
      { 0, 0, 0, 1}
    }};
  return(Result);
}

inline m4x4 ScalingMatrix(f32 Scale) {
  return ScalingMatrix(Scale, Scale, Scale);
}

inline void Print(m4x4 M) {
  for (u32 I = 0; I < 4; ++I)
  {
    for (u32 J = 0; J < 4; ++J)
    {
      printf("%f, ", M.E[I][J]);
    }
    printf("\n");
  }
}

// Unproject applies the inverse of a view projection matrix to the given point
// return its unprojected coordinates. This can be used, for example, to get
// the world space coordinates of the mouse from the clipspace mouse
// coordinates.
v4 Unproject(v4 NormalizedCoordinates, m4x4 Projection, m4x4 View)
{
  m4x4 InverseViewProjection = Inverse(Projection * View);
  return(NormalizedCoordinates * InverseViewProjection);
}

///////////////////////////////////////////////////////////////////////////////
// quat
///////////////////////////////////////////////////////////////////////////////

// NOTE(eric): Composing Rotations
//
// To compose rotations by quaternions Q_1 and Q_2, perform multiplication in
// the reverse order in which you want the rotations to be applied:
//
// Q_12 = Q_2 * Q_1

typedef union quat {
  struct {
    f32 X, Y, Z, W;
  };
  struct {
    v4 V4;
  };
  struct {
    v3 V3;
    f32 _RemainderV3_1;
  };
  struct {
    v2 V2;
    f32 _RemainderV2_1;
    f32 _RemainderV2_2;
  };
  f32 E[4];
} quat;

inline quat Quat(f32 X, f32 Y, f32 Z, f32 W) {
  quat Result = {{X, Y, Z, W}};
  return(Result);
}

inline quat Quat(v2 V) {
  quat Result = {{V.X, V.Y, 0, 0}};
  return(Result);
}

inline quat operator *(quat Left, quat Right) {
  quat Result;
  Result.W = Left.W*Right.W - Left.X*Right.X - Left.Y*Right.Y - Left.Z*Right.Z;
  Result.X = Left.W*Right.X + Left.X*Right.W + Left.Y*Right.Z - Left.Z*Right.Y;
  Result.Y = Left.W*Right.Y + Left.Y*Right.W + Left.Z*Right.X - Left.X*Right.Z;
  Result.Z = Left.W*Right.Z + Left.Z*Right.W + Left.X*Right.Y - Left.Y*Right.X;
  return(Result);
}

inline quat operator *(quat Left, f32 Right) {
  quat Result = Left;
  Result.X *= Right;
  Result.Y *= Right;
  Result.Z *= Right;
  Result.W *= Right;
  return(Result);
}

inline quat operator -(quat Left, quat Right) {
  quat Result;
  Result.X = Left.X-Right.X;
  Result.Y = Left.X-Right.Y;
  Result.Z = Left.Z-Right.Z;
  Result.W = Left.W-Right.W;
  return(Result);
}

inline quat operator *(f32 Left, quat Right) {
  return(Right*Left);
}

inline f32 Norm(quat Q) {
  return Q.X*Q.X + Q.Y*Q.Y + Q.Z*Q.Z + Q.W*Q.W;
}

inline quat NOZ(quat Q) {
  f32 NormQ = Norm(Q);
  return (NormQ > 0.0) ? (Q * (1.0f / NormQ)) : Q;
}

inline m4x4 AsMatrix(quat Quat) {
  f32 NormQ = Norm(Quat);
  f32 Scalar = (NormQ > 0.0) ? (1.0 / NormQ) : 0.0;
  
  f32 Qx = Quat.X * Scalar;
  f32 Qy = Quat.Y * Scalar;
  f32 Qz = Quat.Z * Scalar;
  f32 Qw = Quat.W * Scalar;
  
  // row-major
  // https://stackoverflow.com/questions/1556260/convert-quaternion-rotation-to-rotation-matrix
  m4x4 Result = {{
      {1.0f - 2.0f*Qy*Qy - 2.0f*Qz*Qz, 2.0f*Qx*Qy - 2.0f*Qz*Qw, 2.0f*Qx*Qz + 2.0f*Qy*Qw, 0.0f},
      {2.0f*Qx*Qy + 2.0f*Qz*Qw, 1.0f - 2.0f*Qx*Qx - 2.0f*Qz*Qz, 2.0f*Qy*Qz - 2.0f*Qx*Qw, 0.0f},
      {2.0f*Qx*Qz - 2.0f*Qy*Qw, 2.0f*Qy*Qz + 2.0f*Qx*Qw, 1.0f - 2.0f*Qx*Qx - 2.0f*Qy*Qy, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f}
    }};
  
  return(Result);
}

// Conjugate (q*) is used to compute rotation on a point p as follows: q * p * q*
inline quat Conjugate(quat Quat) {
  quat Result = {{ -Quat.X, -Quat.Y, -Quat.Z, Quat.W }};
  return(Result);
}

inline v2 QuatRotate(v2 Point, quat Q)
{
  quat P = Quat(Point.X, Point.Y, 0, 0);
  quat Result = Q * P * Conjugate(Q);
  return V2(Result.X, Result.Y);
}

inline v3 QuatRotate(v3 Point, quat Q)
{
  quat P = Quat(Point.X, Point.Y, Point.Z, 0);
  quat Result = Q * P * Conjugate(Q);
  return V3(Result.X, Result.Y, Result.Z);
}

inline quat Inverse(quat Quat) {
  quat Result = Conjugate(Quat);
  f32 InverseNorm = 1.0f / Norm(Quat);
  return(Result*InverseNorm);
}

inline quat QuatRotation(v3 AxisOfRotation, f32 AngleRadians) {
  f32 SinAngleOver2 = SinF(AngleRadians/2.0f);
  quat Result = {{
      AxisOfRotation.X*SinAngleOver2,
      AxisOfRotation.Y*SinAngleOver2,
      AxisOfRotation.Z*SinAngleOver2,
      CosF(AngleRadians/2.0f)
    }};
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////
// Collisions
///////////////////////////////////////////////////////////////////////////////

// Represents a circle with the given center point and radius. For use with
// circle collision detection algorithms.
typedef struct circle {
  v2 Center;
  f32 Radius;
} circle;

inline circle Circle(v2 Center, f32 Radius)
{
  circle Result = {};
  Result.Center = Center;
  Result.Radius = Radius;
  return(Result);
}

internal b32 CirclePointIntersect(circle Circle, v2 Point)
{
  b32 Result = false;
  
  // Check if point is inside the circle by seeing if the distance from the
  // center of the circle to the point is less than the radius of the circle.
  if (Length(Circle.Center - Point) < Circle.Radius)
  {
    Result = true;
  }
  
  return(Result);
}

internal b32 CircleCircleIntersect(circle Left, circle Right)
{
  b32 Result = false;
  
  // Check if circles are overlapping by seeing if the distance between their
  // centers is less than the sum of their radii. If it is equal to the sum of
  // their radii then technically they are just touching but we do not count
  // this as a collision.
  if (Length(Left.Center - Right.Center) < (Left.Radius + Right.Radius))
  {
    Result = true;
  }
  
  return(Result);
}

internal b32 RectPointIntersect(v4 Rect, v2 Point)
{
  b32 Result = true;
  
  if (Point.X < Rect.X ||
      Point.X > Rect.X + Rect.Z ||
      Point.Y < Rect.Y ||
      Point.Y > Rect.Y + Rect.W)
  {
    Result = false;
  }
  
  return(Result);
}

internal b32 RectPointIntersect(v4 Rect, v2u Point)
{
  b32 Result = true;
  
  if (Point.X < Rect.X ||
      Point.X > Rect.X + Rect.Z ||
      Point.Y < Rect.Y ||
      Point.Y > Rect.Y + Rect.W)
  {
    Result = false;
  }
  
  return(Result);
}

internal b32 RectPointIntersect(v4 Rect, v2i Point)
{
  b32 Result = true;
  
  if (Point.X < Rect.X ||
      Point.X > Rect.X + Rect.Z ||
      Point.Y < Rect.Y ||
      Point.Y > Rect.Y + Rect.W)
  {
    Result = false;
  }
  
  return(Result);
}

internal b32 RectRectIntersect(v4 Left, v4 Right)
{
  b32 Result = false;
  
  if ((Left.X < (Right.X + Right.Width) && (Left.X + Left.Width) > Right.X) &&
      (Left.Y < (Right.Y + Right.Height) && (Left.Y + Left.Height) > Right.Y)) {
    Result = true;
  }
  
  return(Result);
}

internal b32 CircleRectIntersect(circle Circle, v4 Rect)
{
  b32 Result = false;
  
  // Find the closest rectangle edge to the sphere
  v2 TestPoint = Circle.Center;
  
  if (Circle.Center.X < Rect.X) {
    TestPoint.X = Rect.X;
  } else if (Circle.Center.X > Rect.X + Rect.Width) {
    TestPoint.X = Rect.X + Rect.Width;
  }
  
  if (Circle.Center.Y < Rect.Y) {
    TestPoint.Y = Rect.Y;
  } else if (Circle.Center.Y > Rect.Y + Rect.Height) {
    TestPoint.Y = Rect.Y + Rect.Height;
  }
  
  // Test if the edge is inside of the circle
  if (Length(Circle.Center - TestPoint) < Circle.Radius) {
    Result = true;
  }
  
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////
// golden ratio color generation
///////////////////////////////////////////////////////////////////////////////

// From: https://blog.bruce-hill.com/6-useful-snippets
// Based on: https://martin.ankerl.com/2009/12/09/how-to-create-random-colors-programmatically/
typedef struct color_generator {
  v3 ColorHSV;
  f32 InitialSeed;
} color_generator;

inline color_generator ColorGenerator(f32 InitialSat, f32 InitialVal)
{
  color_generator Result = {};
  Result.InitialSeed = Random01();
  Result.ColorHSV.H = Result.InitialSeed;
  Result.ColorHSV.S = InitialSat;
  Result.ColorHSV.V = InitialVal;
  return(Result);
}

inline void ColorGeneratorReset(color_generator* Generator)
{
  Generator->ColorHSV.H = Generator->InitialSeed;
}

inline v4 ColorGeneratorNextColor(color_generator* Generator)
{
  Generator->ColorHSV.H = FModF(Generator->ColorHSV.H + GOLDEN_RATIO_CONJUGATE, 1.0f);
  v3 Color = HSVToRGB(Generator->ColorHSV);
  return(V4(Color, 1.0f));
}

///////////////////////////////////////////////////////////////////////////////
// strings
///////////////////////////////////////////////////////////////////////////////

// ExtensionInList returns true if the Extension string is in the space-separated ExtensionList
internal bool ExtensionInList(const char *ExtensionList, char *Extension)
{
#if 0
  if (ExtensionList == NULL || Extension == NULL)
  {
    return(false);
  }
#endif
  
  char *Where = strchr(Extension, ' ');
  if (Where || *Extension == '\0')
  {
    return(false);
  }
  
  for (char *Start = (char*)ExtensionList;;)
  {
    Where = strstr(Start, Extension);
    
    if (!Where)
    {
      break;
    }
    
    char *Terminator = Where + strlen(Extension);
    if (Where == Start || *(Where - 1) == ' ')
    {
      if (*Terminator == ' ' || *Terminator == '\0')
      {
        return(true);
      }
    }
    
    Start = Terminator;
  }
  
  return(false);
}

///////////////////////////////////////////////////////////////////////////////
// cross-platform threads/atomics
///////////////////////////////////////////////////////////////////////////////

#if defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)
// Use software (not hardware) memory barriers to serialize reads/writes when
// necessary in multi-threading applications.
#define CompletePreviousReadsBeforeFutureReads asm volatile("" ::: "memory")
#define CompletePreviousWritesBeforeFutureWrites asm volatile("" ::: "memory")

inline u32 AtomicCompareAndExchangeU32(u32 volatile *Value, u32 New, u32 Expected)
{
  u32 Result = __sync_val_compare_and_swap(Value, Expected, New);
  return(Result);
}

inline u32 AtomicExchangeU32(u32 volatile *Value, u32 New)
{
  u32 Result = __sync_lock_test_and_set(Value, New);
  return(Result);
}

inline u64 AtomicExchangeU64(u64 volatile *Value, u64 New)
{
  u64 Result = __sync_lock_test_and_set(Value, New);
  return(Result);
}

inline u32 AtomicAddU32(u32 volatile *Value, u32 Addend)
{
  u64 Result = __sync_fetch_and_add(Value, Addend);
  return(Result);
}

inline u64 AtomicAddU64(u64 volatile *Value, u64 Addend)
{
  u64 Result = __sync_fetch_and_add(Value, Addend);
  return(Result);
}

inline u32 GetThreadID(void)
{
  u32 ThreadID;
  
#if defined(__APPLE__) && defined(__x86_64__)
  asm("mov %%gs:0x00,%0" : "=r"(ThreadID));
#elif defined(__i386__)
  asm("mov %%gs:0x08,%0" : "=r"(ThreadID));
#elif defined(__x86_64__)
  asm("mov %%fs:0x10,%0" : "=r"(ThreadID));
#else
#error "Unsupported architecture"
#endif
  
  return(ThreadID);
}
#elif defined(PLATFORM_WIN)
#define CompletePreviousReadsBeforeFutureReads _ReadBarrier()
#define CompletePreviousWritesBeforeFutureWrites _WriteBarrier()

inline uint32 AtomicCompareExchangeU32(uint32 volatile *Value, uint32 New, uint32 Expected)
{
  uint32 Result = _InterlockedCompareExchange((long volatile *)Value, New, Expected);
  
  return(Result);
}

inline u64 AtomicExchangeU64(u64 volatile *Value, u64 New)
{
  u64 Result = _InterlockedExchange64((__int64 volatile *)Value, New);
  
  return(Result);
}

inline u32 AtomicAddU32(u32 volatile *Value, u32 Addend)
{
  u64 Result = __InterlockedExchangeAdd((long *)Value, Addend);
  return(Result);
}

inline u64 AtomicAddU64(u64 volatile *Value, u64 Addend)
{
  // NOTE(casey): Returns the original value _prior_ to adding
  u64 Result = _InterlockedExchangeAdd64((__int64 volatile *)Va = {};lue, Addend);
  
  return(Result);
}

inline u32 GetThreadID(void)
{
  u8 *ThreadLocalStorage = (u8 *)__readgsqword(0x30);
  u32 ThreadID = *(u32 *)(ThreadLocalStorage + 0x48);
  
  return(ThreadID);
}
#else
#error "Unsupported platform"
#endif

#endif // LANGUAGE_LAYER_H
