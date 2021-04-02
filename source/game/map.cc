#include "map.h"

internal void TilesetCreate(map_tileset *Tileset, const char *TextureHandle, f32 Dim)
{
  strncpy(Tileset->TextureHandle, TextureHandle, 128);
  Tileset->TileSize = Dim;
}

internal v4 TilesetGetSourceRect(map_tileset *Tileset, platform_state *Platform, texture_catalog *TextureCatalog, u32 TileHandle)
{
  v4 Result = V4(0);
  texture Texture = TextureCatalogGet(TextureCatalog, Platform, Tileset->TextureHandle);

  if (Texture.Loaded) {
    v2u DimTiles = V2U(Texture.Dim.Width / (u32)Tileset->TileSize, Texture.Dim.Height / (u32)Tileset->TileSize);
    v2u PosTiles = V2U(TileHandle % DimTiles.Width, TileHandle / DimTiles.Width);

    Assert(PosTiles.X < DimTiles.Width);
    Assert(PosTiles.Y < DimTiles.Height);

    Result = V4(
      PosTiles.X * Tileset->TileSize,
      PosTiles.Y * Tileset->TileSize,
      Tileset->TileSize,
      Tileset->TileSize
    );
  }

  return(Result);
}

///////////////////////////////////////////////////////////////////////////////

internal void MapCreate(map *Map, texture_catalog *TextureCatalog, map_tileset *Tileset, v2u Dim)
{
  Map->TextureCatalog = TextureCatalog;
  Map->Tileset = Tileset;
  Map->Dim = Dim;
  Map->TileSize = 128;
  foreach(Layer, MAP_LAYERS_MAX) {
    foreach(Y, MAP_HEIGHT_MAX) {
      foreach(X, MAP_WIDTH_MAX) {
        Map->Tiles[Layer][Y][X] = MAP_TILE_EMPTY;
      }
    }
  }
}

internal void MapSetTile(map *Map, u32 Layer, u32 X, u32 Y, u16 TileHandle)
{
  Assert(Layer < MAP_LAYERS_MAX);
  Assert(X < Map->Dim.Width);
  Assert(Y < Map->Dim.Height);
  Map->Tiles[Layer][Y][X] = TileHandle;
}

internal u16 MapGetTile(map *Map, u32 Layer, u32 X, u32 Y)
{
  Assert(Layer < MAP_LAYERS_MAX);
  Assert(X < Map->Dim.Width);
  Assert(Y < Map->Dim.Height);
  return Map->Tiles[Layer][Y][X];
}

internal void MapRenderLayer(map *Map, app_context Ctx, u32 Layer)
{
  Assert(Layer < MAP_LAYERS_MAX);
  foreach(Y, Map->Dim.Height) {
    foreach(X, Map->Dim.Width) {
      if (Map->Tiles[Layer][Y][X] == MAP_TILE_EMPTY) {
        continue;
      }

      v4 Source = TilesetGetSourceRect(Map->Tileset, Ctx.Platform, Map->TextureCatalog, Map->Tiles[Layer][Y][X]);
      // NOTE: Map coordinates run from (0, 0) in the top-left to (Width - 1,
      // Height - 1) in the bottom right. However, our rendering coordinates
      // run from (0, 0) in the bottom left to (Width - 1, Height - 1) in the
      // upper right. Therefore, we need to invert the Y coordinates to ensure
      // that rendering happens as expected.
      RendererPushTexture(
        &Ctx.Game->Renderer,
        RENDER_FLAG_fat_pixel,
        TextureCatalogGet(Map->TextureCatalog, Ctx.Platform, Map->Tileset->TextureHandle),
        Source,
        V4(X * Map->TileSize, (Map->Dim.Height - Y - 1) * Map->TileSize, Map->TileSize, Map->TileSize),
        V4(1)
      );
    }
  }
}

internal void MapRenderAllLayers(map *Map, app_context Ctx)
{
  foreach(Layer, MAP_LAYERS_MAX) {
    MapRenderLayer(Map, Ctx, Layer);
  }
}

internal void MapDebugRender(map *Map, app_context Ctx)
{
  foreach(Y, Map->Dim.Height) {
    foreach(X, Map->Dim.Width) {
      RendererPushUnfilledRect(
        &Ctx.Game->Renderer,
        0,
        V4(X * Map->TileSize, Y * Map->TileSize, Map->TileSize, Map->TileSize),
        V4(1)
      );
    }
  }
}
