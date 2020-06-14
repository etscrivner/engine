#ifndef GAME_RENDERER_H
#define GAME_RENDERER_H

#include "common/language_layer.h"
#include "common/memory_arena.h"
#include "game.h"
#include "shaders.h"

#define MAX_COLOR_TEXTURES_PER_FRAME_BUFFER 8

typedef enum framebuffer_texture_format {
  FRAMEBUFFER_TEXTURE_FORMAT_invalid,
  FRAMEBUFFER_TEXTURE_FORMAT_rgba,
  FRAMEBUFFER_TEXTURE_FORMAT_hdr,
  FRAMEBUFFER_TEXTURE_FORMAT_MAX
} framebuffer_texture_format;

typedef struct framebuffer {
  u32 Width;
  u32 Height;

  GLuint FBO;
  // NOTE(eric): Guaranteed to be able to have up to 8 attachments per
  // Framebuffer. These additional attachments can be used, for example, with
  // glDrawBuffers to render data into multiple textures at once.
  //
  // We only support one texture attachment for now until we have an actual
  // use-case for more texture attachments.
  GLuint TextureAttachment;
  framebuffer_texture_format TextureAttachmentFormat;
} framebuffer;

typedef struct renderer {
  v2 Dim;
  v4 ClearColor;
  char *Extensions;
} renderer;

///////////////////////////////////////////////////////////////////////////////
// frame_buffer
///////////////////////////////////////////////////////////////////////////////

internal framebuffer FramebufferCreate(u32 Width, u32 Height);
internal void FramebufferDestroy(framebuffer *Framebuffer);
internal b32 FramebufferIsValid(framebuffer *Framebuffer);
internal void FramebufferMaybeResize(framebuffer *Framebuffer, v2 RenderDim);
internal void FramebufferAttachTexture(framebuffer *Framebuffer, framebuffer_texture_format Format);
internal void FramebufferBindToTexture(framebuffer *Framebuffer, GLuint Texture);

///////////////////////////////////////////////////////////////////////////////
// renderer
///////////////////////////////////////////////////////////////////////////////

internal void RendererCreate(memory_arena *TransientArena, platform_state *Platform, renderer *Renderer);
internal void RendererDestroy(renderer *Renderer);
internal void RendererBeginFrame(renderer *Renderer, platform_state *Platform, v2 Dim, v4 ClearColor);
internal void RendererEndFrame(renderer *Renderer);

internal void RendererSetTarget(renderer *Renderer, framebuffer *Target);
internal void RendererClearTarget(renderer *Renderer);

#define GLProc(Type, Name) internal PFNGL##Type##PROC gl##Name = NULL;
#include "opengl_procedure_list.h"

internal PFNGLCLIPCONTROLPROC glClipControl = NULL;

#endif // GAME_RENDERER_H
