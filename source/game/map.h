#ifndef GAME_MAP_H
#define GAME_MAP_H

#include "common/language_layer.h"

#define MAP_WIDTH_MAX 128
#define MAP_HEIGHT_MAX 128
#define MAP_LAYERS_MAX 16
#define MAP_OBSTACLES_MAX 64
#define MAP_TILE_EMPTY 0xFFFF

typedef struct app_context app_context;

typedef enum map_zone_type {
  ZONE_TYPE_walkover, // Zone triggered by player walking over it
  ZONE_TYPE_interact, // Zone triggered by player interaction when nearby
  ZONE_TYPE_MAX
} map_zone_type;

// A zone ID is a globally unique (per map) identifier for a zone and is used
// to identify which interactions to trigger with the given zone.
typedef struct zone_id {
  char ID[32];
} zone_id;

internal b32 operator ==(zone_id& Left, zone_id &Right) {
  return(strncmp(Left.ID, Right.ID, 32) == 0);
}

// A zone is a special area on a map which produces some sort of
// interaction. Zones can be interacted with either by the player going over
// specific tiles or by the player pressing an interaction button when facing a
// given tile or set of tiles.
typedef struct map_zone {
  zone_id ID;
  map_zone_type Type;
  v2 BottomLeft; // Bottom left corner of zone rect (units of tiles).
  v2 Size; // Width and height of zone (units of tiles).
} map_zone;

// A tileset represents a single texture that is divided into identically sized
// tiles which are used to construct a map.
typedef struct map_tileset {
  char TextureHandle[128];
  f32 TileSize;
} map_tileset;

internal void TilesetCreate(map_tileset *Tileset, const char *TextureHandle, f32 Dim);
internal v4 TilesetGetSourceRect(map_tileset *Tileset, platform_state *Platform, texture_catalog *TextureCatalog, u32 TileHandle);

typedef struct map {
  map_tileset *Tileset;
  v2u Dim; // Map dimensions in units of tiles
  u16 Tiles[MAP_LAYERS_MAX][MAP_WIDTH_MAX][MAP_HEIGHT_MAX]; // Tile data
  f32 TileSize; // Width and Height in which individual tiles are rendered.
  u32 NumObstacles; // Number of obstacles on this map
  v4 Obstacles[MAP_OBSTACLES_MAX]; // Obstructions that should prevent the player from moving.
  texture_catalog *TextureCatalog;
} map;

internal void MapCreate(map *Map, texture_catalog *TextureCatalog, map_tileset* Tileset, v2u Dim);
internal void MapSetTile(map *Map, u32 Layer, u32 X, u32 Y, u16 TileHandle);
internal u16 MapGetTile(map *Mpa, u32 Layer, u32 X, u32 Y);
internal void MapRenderLayer(map *Map, app_context Ctx, u32 Layer);
internal void MapRenderAllLayers(map *Map, app_context Ctx);
internal void MapDebugRender(map *Map, app_context Ctx);

#endif // GAME_MAP_H
