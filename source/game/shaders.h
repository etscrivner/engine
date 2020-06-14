#ifndef GAME_SHADERS_H
#define GAME_SHADERS_H

#include "common/watched_file.h"

#define SHADER_CATALOG_MAX_SHADERS 64

typedef struct game_state game_state;

typedef struct shader {
  GLuint Program;

  struct {
    watched_file ShaderFile;
    char ShaderFileName[128];
  } Internal;
} shader;

#if 0
{
  // Shader Catalog Design:
  // + Catalog Gets Its Own Arena  shader_catalog ShaderCatalog;
  ShaderCatalogInit(&ShaderCatalog);

  ShaderCatalogGet(&ShaderCatalog, Platform, GameState, "fxaa.gl");
}
#endif

internal void ShaderLoad(shader *Shader, platform_state *Platform, game_state *GameState, char *ShaderFile);
internal void ShaderDestroy(shader *Shader);
internal void ShaderUse(shader *Shader);

#endif // GAME_SHADERS_H
