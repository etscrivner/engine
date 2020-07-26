#ifndef GAME_SHADERS_H
#define GAME_SHADERS_H

#include "common/watched_file.h"

#define SHADER_CATALOG_MAX_SHADERS 64
#define SHADER_CATALOG_REFERENCE_NAME_MAX_SIZE 32

typedef struct game_state game_state;

typedef struct shader {
  GLuint Program;
  
  struct {
    watched_file ShaderFile;
    char ShaderFileName[128];
  } Internal;
} shader;

typedef struct shader_catalog_entry {
  GLuint Program;
  i32 WatcherHandle;
  char ReferenceName[SHADER_CATALOG_REFERENCE_NAME_MAX_SIZE];
} shader_catalog_entry;

typedef struct shader_catalog {
  memory_arena *TransientArena;
  watched_file_set Watcher;
  u32 NumEntries;
  shader_catalog_entry Entry[SHADER_CATALOG_MAX_SHADERS];
} shader_catalog;

internal b32 ShaderCatalogInit(shader_catalog *Catalog, memory_arena *TransientArena);
internal void ShaderCatalogDestroy(shader_catalog *Catalog);
internal b32 ShaderCatalogAdd(shader_catalog *Catalog, platform_state *Platform, char *ShaderFile, char *ReferenceName);
internal b32 ShaderCatalogRemove(shader_catalog *Catalog, char *ReferenceName);
internal GLuint ShaderCatalogGet(shader_catalog *Catalog, char *ReferenceName);
internal GLuint ShaderCatalogUse(shader_catalog *Catalog, char *ReferenceName);
internal b32 ShaderCatalogUpdate(shader_catalog *Catalog, platform_state *Platform);

internal void ShaderLoad(shader *Shader, platform_state *Platform, game_state *GameState, char *ShaderFile);
internal void ShaderDestroy(shader *Shader);
internal void ShaderUse(shader *Shader);

#endif // GAME_SHADERS_H
