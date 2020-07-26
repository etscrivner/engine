#ifndef GAME_TEXTURES_H
#define GAME_TEXTURES_H

#include "common/language_layer.h"

#define TEXTURE_CATALOG_MAX_TEXTURES 512
#define TEXTURE_CATALOG_REFERENCE_NAME_MAX_SIZE 32

typedef struct texture {
  b32 Loaded;
  GLuint ID;
  v2 Dim;
} texture;

typedef struct texture_catalog_entry {
  texture Texture;
  i32 WatcherHandle;
  char ReferenceName[TEXTURE_CATALOG_REFERENCE_NAME_MAX_SIZE];
} texture_catalog_entry;

typedef struct texture_catalog {
  watched_file_set Watcher;
  u32 volatile NumEntries;
  texture_catalog_entry Entry[TEXTURE_CATALOG_MAX_TEXTURES];
  
  // TODO: @NoCommit: Remove me
  b32 AllowAsync;
} texture_catalog;

internal b32 TextureCatalogInit(texture_catalog *Catalog);
internal void TextureCatalogDestroy(texture_catalog *Catalog);
internal b32 TextureCatalogAdd(texture_catalog *Catalog, char *TextureFile, char *ReferenceName);
internal texture TextureCatalogGet(texture_catalog *Catalog, platform_state *Platform, char *ReferenceName);

internal b32 TextureCatalogUpdate(texture_catalog *Catalog, platform_state *Platform);

#endif // GAME_TEXTURES_H
