#ifndef GAME_FONTS_H
#define GAME_FONTS_H

typedef struct font_glyph_cache {
  u8 Char;
  v2 Dim;
  v2 Source;
  v2 Bearing;
  u32 Advance;
  b32 Loaded;
} font_glyph_cache;

typedef struct font {
  const char *FontFile;
  u32 FontSizePixels;
  FT_Face Face;
  GLuint Texture;
  v2 TextureDim;
  font_glyph_cache GlyphCache[128];
} packed_font;

typedef struct font_manager {
  FT_Library FreeType;
  const char *FontDirectory;
} font_manager;

internal b32 FontManagerInit(font_manager *FontManager, const char *FontDirectory);
internal void FontManagerDestroy(font_manager *FontManager);
internal b32 FontManagerLoadFont(
  font_manager *FontManager,
  font *Font,
  const char *FontFile,
  u32 FontSizePixels,
  memory_arena *TransientArena
);
internal void FontManagerDestroyFont(font_manager *FontManager, font *Font);

internal f32 FontTextWidthPixels(font *Font, const char *Text);
internal f32 FontTextRangeWidthPixels(font *Font, const char *Text, u32 Start, u32 Stop);
internal f32 FontTextPrefixWidthPixels(font *Font, const char *Text, u32 Length);
internal f32 FontTextHeightPixels(font *Font);
internal f32 FontAscenderPixels(font *Font);
internal f32 FontDescenderPixels(font *Font);
internal f32 FontBaselinePixels(font *Font);
internal f32 FontCenterOffset(font *Font, f32 Height);

// Converts an X-Offset in pixels to the character at that offset in the given
// text string. Returns -1 if the offset is out of range.
internal i32 FontTextPixelOffsetToIndex(font *Font, const char *Text, f32 XOffset);

#endif // GAME_FONTS_H
