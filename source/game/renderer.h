#ifndef GAME_RENDERER_H
#define GAME_RENDERER_H

#include "common/language_layer.h"
#include "common/memory_arena.h"

#include "shaders.h"
#include "fonts.h"
#include "textures.h"

#define RENDERER_REQUESTS_MAX 65536
#define RENDERER_CLIP_STACK_MAX 128
#define RENDERER_MVP_MATRIX_STACK_MAX 16

#define RENDERER_LINES_MAX 16384
#define RENDERER_FILLED_RECT_MAX 16384
#define RENDERER_FILLED_CIRCLE_MAX 16384
#define RENDERER_TEXTURED_QUADS_MAX 16384
#define RENDERER_TEXTS_MAX 16384

typedef enum render_request_type {
  RENDER_REQUEST_null,
  RENDER_REQUEST_line,
  RENDER_REQUEST_filled_rect,
  RENDER_REQUEST_filled_circle,
  RENDER_REQUEST_textured_quad,
  RENDER_REQUEST_text,
  RENDER_REQUEST_set_clip,
  RENDER_REQUEST_set_mvp_matrix,
  RENDER_REQUEST_MAX
} render_request_type;

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

// Represents VAO/VBO combination used for providing vertex data for rendering
// a specific primitive in an indexed fashion.
typedef struct indexed_render_buffer {
  GLuint VAO;
  GLuint VBO;
  
  u32 NumItems;
  size_t ItemSizeBytes;
  size_t TotalSizeBytes;
} indexed_render_buffer;

// Represents a batch of similar drawing commands along with optional metadata
// required to render those commands.
typedef struct render_request {
  render_request_type Type;
  u32 DataOffset;
  u32 DataSize;
  u32 Flags;
  
  union {
    struct {
      GLuint TextureID;
      v2 Dim;
    } TexturedQuad;
    
    struct {
      v2 PackedTextureDim;
      GLuint TextureID;
    } Text;

    struct {
      v4 Rect;
    } Clip;

    struct {
      m4x4 MVP;
    } MVPMatrix;
  };
} render_request;

typedef struct renderer {
  v2u Dim;
  char *Extensions;
  shader_catalog *ShaderCatalog;
  
  u32 NumRequests;
  render_request ActiveRequest;
  render_request Request[RENDERER_REQUESTS_MAX];
  
  // Instanced rendering
  indexed_render_buffer LineBuffer;
  u32 LineInstanceDataPos;
  // NOTE: x,y  x,y  r,g,b,a
#define RENDERER_BYTES_PER_LINE (sizeof(f32) * 8)
  u8 LineInstanceData[RENDERER_LINES_MAX * RENDERER_BYTES_PER_LINE];
  
  indexed_render_buffer FilledRectBuffer;
  u32 FilledRectInstanceDataPos;
  // NOTE: x,y x,y x,y x,y r,g,b,a r,g,b,a r,g,b,a r,g,b,a
#define RENDERER_BYTES_PER_FILLED_RECT (sizeof(f32) * 24)
  u8 FilledRectInstanceData[RENDERER_FILLED_RECT_MAX * RENDERER_BYTES_PER_FILLED_RECT];
  
  indexed_render_buffer FilledCircleBuffer;
  u32 FilledCircleInstanceDataPos;
  // NOTE: x,y x,y x,y x,y x,y,radius r,g,b,a
#define RENDERER_BYTES_PER_FILLED_CIRCLE (sizeof(f32) * 15)
  u8 FilledCircleInstanceData[RENDERER_FILLED_CIRCLE_MAX * RENDERER_BYTES_PER_FILLED_CIRCLE];
  
  indexed_render_buffer TexturedQuadBuffer;
  u32 TexturedQuadInstanceDataPos;
  // NOTE: x,y,w,h x,y x,y x,y x,y r,g,b,a r,g,b,a r,g,b,a r,g,b,a
#define RENDERER_BYTES_PER_TEXTURED_QUAD (sizeof(f32) * 28)
  u8 TexturedQuadInstanceData[RENDERER_TEXTURED_QUADS_MAX * RENDERER_BYTES_PER_TEXTURED_QUAD];
  
  indexed_render_buffer TextBuffer;
  u32 TextInstanceDataPos;
  // NOTE: x,y,w,h(dest) x,y,w,h(src) r,g,b,a
#define RENDERER_BYTES_PER_TEXT (sizeof(f32) * 12)
  u8 TextInstanceData[RENDERER_TEXTS_MAX * RENDERER_BYTES_PER_TEXT];
  
  // Clipping stack
  v4 ClipRect;
  u32 ClipStackCount;
  v4 ClipStack[RENDERER_CLIP_STACK_MAX];
  
  // Model-view-projection matrix stack
  m4x4 MVPMatrix;
  u32 MVPStackCount;
  m4x4 MVPStack[RENDERER_MVP_MATRIX_STACK_MAX];
} renderer;

///////////////////////////////////////////////////////////////////////////////
// frame_buffer
///////////////////////////////////////////////////////////////////////////////

internal framebuffer FramebufferCreate(u32 Width, u32 Height);
internal void FramebufferDestroy(framebuffer *Framebuffer);
internal b32 FramebufferIsValid(framebuffer *Framebuffer);
internal void FramebufferMaybeResize(framebuffer *Framebuffer, v2i RenderDim);
internal void FramebufferAttachTexture(framebuffer *Framebuffer, framebuffer_texture_format Format);
internal void FramebufferBindToTexture(framebuffer *Framebuffer, GLuint Texture);

///////////////////////////////////////////////////////////////////////////////
// indexed_render_buffer
///////////////////////////////////////////////////////////////////////////////

internal indexed_render_buffer IndexedRenderBufferCreate(u32 NumItems, size_t ItemSizeBytes, void *VertexData=0);
internal void IndexedRenderBufferDestroy(indexed_render_buffer *Buffer);
internal void IndexedRenderBufferSetAttrib(indexed_render_buffer *Buffer, u32 Index, u32 NumFloatVals, size_t AttribOffsetBytes);

///////////////////////////////////////////////////////////////////////////////
// renderer
///////////////////////////////////////////////////////////////////////////////

internal void RendererCreate(platform_state *Platform, renderer *Renderer, shader_catalog *ShaderCatalog);
internal void RendererDestroy(renderer *Renderer);
internal void RendererBeginFrame(renderer *Renderer, platform_state *Platform, v2i Dim);
internal void RendererEndFrame(renderer *Renderer);
internal void RendererFlush(renderer *Renderer);
internal void RendererClear(renderer* Renderer, v4 ClearColor);

internal void RendererSetTarget(renderer *Renderer, framebuffer *Target);
internal void RendererClearTarget(renderer *Renderer);

internal void Renderer2DRightHanded(renderer *Renderer, v2i Dim);

internal void RendererFinishActiveRequest(renderer *Renderer);
internal void RendererPushLine(renderer *Renderer, u32 Flags, v2 Start, v2 End, v4 Color);
internal inline void RendererPushFilledRect(renderer *Renderer, u32 Flags, v4 Rect, v4 Color);
internal void RendererPushFilledCircle(renderer *Renderer, u32 Flags, v2 Center, f32 Radius, v4 Color);
internal void RendererPushTexturedQuad(renderer* Renderer, u32 Flags, GLuint TextureID, v2 TextureDim, v4 SourceRect, v4 DestRect, v4 Color);
internal inline void RendererPushTexture(renderer *Renderer, u32 Flags, texture Texture, v4 SourceRect, v4 DestRect, v4 Color);
internal void RendererPushText(renderer *Renderer, u32 Flags, font *Font, const char* Text, v2 Pos, v4 Color);

// Clipping
internal void RendererPushClip(renderer *Renderer, v4 ClipRect);
internal void RendererPopClip(renderer *Renderer);

// Model-view-projection matrix
internal void RendererPushMVPMatrix(renderer *Renderer, m4x4 MVP);
internal void RendererPopMVPMatrix(renderer *Renderer);

// OpenGL procedures we need to load from the library
#define GLProc(Type, Name) internal PFNGL##Type##PROC gl##Name = NULL;
#include "opengl_procedure_list.h"

internal PFNGLCLIPCONTROLPROC glClipControl = NULL;

#endif // GAME_RENDERER_H
