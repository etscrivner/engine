#include "fonts.h"

internal b32 FontManagerInit(font_manager *FontManager, const char *FontDirectory)
{
  b32 Result = true;
  
  if (FT_Init_FreeType(&FontManager->FreeType))
  {
    fprintf(stderr, "error: unable to initialize freetype.\n");
    Result = false;
  }
  
  FontManager->FontDirectory = FontDirectory;
  
  return(Result);
}

internal void FontManagerDestroy(font_manager *FontManager)
{
  FT_Done_FreeType(FontManager->FreeType);
}

internal b32 FontManagerLoadFont(font_manager *FontManager, font *Font, const char *FontFile, u32 FontSizePixels)
{
  local_persist char FontFullPath[256];
  snprintf(FontFullPath, 256, "%s/%s", FontManager->FontDirectory, FontFile);
  
  b32 Result = true;
  if (FT_New_Face(FontManager->FreeType, FontFullPath, 0, &Font->Face))
  {
    fprintf(stderr, "error: unable to load font face '%s'\n", FontFullPath);
    Result = false;
  }
  else
  {
    Font->FontSizePixels = FontSizePixels;
    FT_Set_Pixel_Sizes(Font->Face, 0, FontSizePixels);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(ArrayCount(Font->GlyphTextures), Font->GlyphTextures);
    for (u32 I = 0; I < ArrayCount(Font->GlyphCache); ++I)
    {
      if (FT_Load_Char(Font->Face, (u8)I, FT_LOAD_RENDER))
      {
        fprintf(stderr, "error: failed to load glyph %d\n", I);
        continue;
      }
      
      font_glyph_cache *Glyph = Font->GlyphCache + I;
      Glyph->Char = (u8)I;
      Glyph->Dim = V2(Font->Face->glyph->bitmap.width, Font->Face->glyph->bitmap.rows);
      Glyph->Bearing = V2(Font->Face->glyph->bitmap_left, Font->Face->glyph->bitmap_top);
      Glyph->Advance = Font->Face->glyph->advance.x;
      Glyph->Loaded = true;
      
      glBindTexture(GL_TEXTURE_2D, Font->GlyphTextures[I]);
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   GL_RED,
                   Font->Face->glyph->bitmap.width,
                   Font->Face->glyph->bitmap.rows,
                   0,
                   GL_RED,
                   GL_UNSIGNED_BYTE,
                   Font->Face->glyph->bitmap.buffer);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  
  return(Result);
}

internal void FontManagerDestroyFont(font_manager *FontManager, font *Font)
{
  FT_Done_Face(Font->Face);
  glDeleteTextures(ArrayCount(Font->GlyphTextures), Font->GlyphTextures);
}

internal f32 FontTextWidthPixels(font *Font, const char *Text)
{
  char *NextCh = (char*)Text;
  f32 TotalWidth = 0;
  while (*NextCh)
  {
    font_glyph_cache *Cached = Font->GlyphCache + (u32)(*NextCh);
    if (*NextCh == '\t') {
      TotalWidth += (Cached->Advance >> 6) * 4;
    } else {
      TotalWidth += (Cached->Advance >> 6);
    }
    ++NextCh;
  }
  
  return(TotalWidth);
}

internal f32 FontTextRangeWidthPixels(font *Font, const char *Text, u32 Start, u32 Stop)
{
  Assert(Start <= Stop);
  char *NextCh = (char*)Text + Start;
  char *EndCh = (char*)Text + Stop;
  f32 TotalWidth = 0.0f;
  while (*NextCh && NextCh < EndCh) {
    font_glyph_cache *Cached = Font->GlyphCache + (u32)(*NextCh);
    if (*NextCh == '\t') {
      TotalWidth += (Cached->Advance >> 6) * 4;
    } else {
      TotalWidth += (Cached->Advance >> 6);
    }
    ++NextCh;
  }
  
  return(TotalWidth);
}

internal f32 FontTextHeightPixels(font *Font)
{
  // NOTE: Divide by 64 as units are 1/64th pixel.
  return(Font->FontSizePixels);
  //return(Font->Face->size->metrics.height >> 6);
}

internal f32 FontAscenderPixels(font *Font)
{
  return(Font->Face->size->metrics.ascender >> 6);
}

internal f32 FontDescenderPixels(font *Font)
{
  return(Font->Face->size->metrics.descender >> 6);
}

internal i32 FontTextPixelOffsetToIndex(font *Font, const char *Text, f32 XOffset)
{
  i32 Result = -1;
  char *NextCh = (char*)Text;
  
  i32 Index = 0;
  f32 XStart = 0.0f;
  while (*NextCh)
  {
    font_glyph_cache *Cached = Font->GlyphCache + (u32)(*NextCh);
    f32 GlyphOffset = (Cached->Advance >> 6);
    if (*NextCh == '\t') {
      GlyphOffset = (Cached->Advance >> 6) * 4;
    }

    if (XOffset >= XStart && XOffset <= (XStart + GlyphOffset))
    {
      Result = Index;
      break;
    }
    
    ++NextCh;
    ++Index;
    XStart += GlyphOffset;
  }
  
  return(Result);
}
