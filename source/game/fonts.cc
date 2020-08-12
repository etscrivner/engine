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

internal b32 FontManagerLoadFont(
  font_manager *FontManager,
  packed_font *Font,
  const char *FontFile,
  u32 FontSizePixels,
  memory_arena *TransientArena
)
{
  local_persist char FontFullPath[256];
  snprintf(FontFullPath, 256, "%s/%s", FontManager->FontDirectory, FontFile);

  scoped_arena TextureArena(TransientArena);
  Font->TextureDim = V2(500, 500);
  u8 *TextureData = ScopedArenaPushArray(&TextureArena, (u32)Font->TextureDim.Width * (u32)Font->TextureDim.Height, u8);

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

    // Attempt to pack all rects into our texture
    stbrp_context Packer;
    u32 NodeCount = ArrayCount(Font->GlyphCache);
    stbrp_node *Nodes = ScopedArenaPushArray(&TextureArena, sizeof(stbrp_node) * NodeCount, stbrp_node);
    stbrp_init_target(&Packer, 500, 500, Nodes, NodeCount);

    stbrp_rect *Rects = ScopedArenaPushArray(&TextureArena, sizeof(stbrp_rect) * NodeCount, stbrp_rect);

    // NOTE: We add some padding around each rectangle so that bilinear
    // sampling from the final texture doesn't produce artifacts at the edges
    // of the characters.
    i32 PaddingPixels = 1;
    foreach(I, NodeCount)
    {
      if (FT_Load_Char(Font->Face, (u8)I, FT_LOAD_RENDER))
      {
        fprintf(stderr, "error: failed to load glyph %d\n", I);
        continue;
      }

      stbrp_rect *Rect = Rects + I;
      Rect->id = I;
      Rect->w = Font->Face->glyph->bitmap.width + PaddingPixels;
      Rect->h = Font->Face->glyph->bitmap.rows + PaddingPixels;
    }

    stbrp_pack_rects(&Packer, Rects, NodeCount);

    // Iterate over packed rects and verify that all of them were packed
    foreach (I, NodeCount)
    {
      if (!Rects[I].was_packed)
      {
        fprintf(stderr, "error: failed to pack rect %d (%dx%d)\n", Rects[I].id, Rects[I].w, Rects[I].h);
        Result = false;
      }
    }

    // If all glyphs were successfully packed, then render them into the texture data
    if (Result)
    {
      foreach (I, NodeCount)
      {
        stbrp_rect *Rect = Rects + I;
        FT_Load_Char(Font->Face, (u8)Rect->id, FT_LOAD_RENDER);

        font_glyph_cache *Glyph = Font->GlyphCache + Rect->id;
        Glyph->Char = (u8)Rect->id;
        Glyph->Dim = V2(Font->Face->glyph->bitmap.width, Font->Face->glyph->bitmap.rows);
        Glyph->Source = V2(Rect->x + PaddingPixels, Rect->y + PaddingPixels);
        Glyph->Bearing = V2(Font->Face->glyph->bitmap_left, Font->Face->glyph->bitmap_top);
        Glyph->Advance = Font->Face->glyph->advance.x;
        Glyph->Loaded = true;

        for (u32 Y = 0; Y < Glyph->Dim.Height; ++Y)
        {
          u8* Row = TextureData + (Rect->x + PaddingPixels + 500 * (Y + PaddingPixels + Rect->y));
          for (u32 X = 0; X < Glyph->Dim.Width; ++X)
          {
            *Row++ = Font->Face->glyph->bitmap.buffer[X + ((u32)Font->Face->glyph->bitmap.pitch * Y)];
          }
        }
      }

      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glGenTextures(1, &Font->Texture);
      glBindTexture(GL_TEXTURE_2D, Font->Texture);
      glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RED,
        500,
        500,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        TextureData
      );
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
  }

  return(Result);
}

internal void FontManagerDestroyFont(font_manager *FontManager, font *Font)
{
  FT_Done_Face(Font->Face);
  glDeleteTextures(1, &Font->Texture);
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
  return(Font->Face->size->metrics.height >> 6);
}

internal f32 FontAscenderPixels(font *Font)
{
  return(Font->Face->size->metrics.ascender >> 6);
}

internal f32 FontDescenderPixels(font *Font)
{
  return(Font->Face->size->metrics.descender >> 6);
}

internal f32 FontBaselinePixels(font *Font)
{
  return(FontAscenderPixels(Font) + FontDescenderPixels(Font));
}

internal f32 FontCenterOffset(font *Font, f32 Height)
{
  return (Height / 2 - Round(FontBaselinePixels(Font) / 2));
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
