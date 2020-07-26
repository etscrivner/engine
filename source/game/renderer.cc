#include "renderer.h"

///////////////////////////////////////////////////////////////////////////////
// types

typedef struct glenum_to_string {
  GLenum Key;
  const char* Value;
} glenum_to_string;

///////////////////////////////////////////////////////////////////////////////
// forward definitions for internal methods

internal void OpenGLDebugMessageCallback(
                                         GLenum Source,
                                         GLenum Type,
                                         GLuint ID,
                                         GLenum Severity,
                                         GLsizei MessageLength,
                                         const GLchar *Message,
                                         const void *UserParam
                                         );
internal void OpenGLLoadProcedures(platform_state *Platform, const char *ExtensionList);
internal void OpenGLInit(platform_state *Platform, const char *ExtensionList);

///////////////////////////////////////////////////////////////////////////////
// framebuffer

internal framebuffer FramebufferCreate(u32 Width, u32 Height)
{
  framebuffer Result = {
    .Width  = Width,
    .Height = Height,
  };
  
  glGenFramebuffers(1, &Result.FBO);
  
  // NOTE(eric): Framebuffer is not valid until it is first bound.
  glBindFramebuffer(GL_FRAMEBUFFER, Result.FBO);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  
  return(Result);
}

internal void FramebufferDestroy(framebuffer *Framebuffer)
{
  Assert(glIsFramebuffer(Framebuffer->FBO));
  glDeleteFramebuffers(1, &Framebuffer->FBO);
  
  if (glIsTexture(Framebuffer->TextureAttachment))
  {
    glDeleteTextures(1, &Framebuffer->TextureAttachment);
  }
}

internal b32 FramebufferIsValid(framebuffer *Framebuffer)
{
  b32 Result = false;
  if (glIsFramebuffer(Framebuffer->FBO))
  {
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer->FBO);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
      Result = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  
  return(Result);
}

internal void FramebufferMaybeResize(framebuffer *Framebuffer, v2 RenderDim)
{
  // Sanity check, as sometimes RenderDim is (0, 0) during early life-cycle
  if (RenderDim.Width <= 1 || RenderDim.Height < 1)
  {
    return;
  }

  // Resize the framebuffer if the render dimensions changed
  if (RenderDim.Width != (f32)Framebuffer->Width || RenderDim.Height != (f32)Framebuffer->Height)
  {
    framebuffer_texture_format OldFormat = Framebuffer->TextureAttachmentFormat;
    
    if (glIsFramebuffer(Framebuffer->FBO))
    {
      FramebufferDestroy(Framebuffer);
    }
    *Framebuffer = FramebufferCreate((u32)RenderDim.Width, (u32)RenderDim.Height);
    
    if (OldFormat != FRAMEBUFFER_TEXTURE_FORMAT_invalid)
    {
      FramebufferAttachTexture(Framebuffer, OldFormat);
    }
  }
}

internal void FramebufferAttachTexture(framebuffer *Framebuffer, framebuffer_texture_format Format)
{
  Assert(glIsFramebuffer(Framebuffer->FBO));
  Assert(!glIsTexture(Framebuffer->TextureAttachment));
  
  Framebuffer->TextureAttachmentFormat = Format;
  
  glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer->FBO);
  {
    glGenTextures(1, &Framebuffer->TextureAttachment);
    glBindTexture(GL_TEXTURE_2D, Framebuffer->TextureAttachment);
    //glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, Framebuffer->TextureAttachment);
    {
      GLuint GLFormat = (Format == FRAMEBUFFER_TEXTURE_FORMAT_hdr) ? GL_RGBA16F : GL_RGBA;
      // glTexImage2DMultisample(
      //   GL_TEXTURE_2D_MULTISAMPLE, 8, GLFormat,
      //   Framebuffer->Width, Framebuffer->Height,
      //   GL_TRUE
      // );
      glTexImage2D(
        GL_TEXTURE_2D, 0, GLFormat,
        Framebuffer->Width, Framebuffer->Height,
        0, GL_RGBA, GL_FLOAT, NULL
      );
      
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Framebuffer->TextureAttachment, 0);

    // Debug framebuffer incomplete issues
#if 0
    printf("FBO Status (%dx%d): %d (%d)\n", Framebuffer->Width, Framebuffer->Height, glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    GLenum Error = glGetError();
    if (Error != GL_NO_ERROR)
    {
      fprintf(stderr, "\n[RendererEndFrame] OpenGL Error %i: %s\n", (int)Error, gluErrorString(Error));
    }
#endif

    //glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, Framebuffer->TextureAttachment, 0);
    Assert(FramebufferIsValid(Framebuffer));
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

internal void FramebufferBindToTexture(framebuffer *Framebuffer, GLuint Texture)
{
  glActiveTexture(Texture);
  glBindTexture(GL_TEXTURE_2D, Framebuffer->TextureAttachment);
  //glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, Framebuffer->TextureAttachment);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

///////////////////////////////////////////////////////////////////////////////
// indexed_render_buffer

internal indexed_render_buffer IndexedRenderBufferCreate(u32 NumItems, size_t ItemSizeBytes, void *VertexData)
{
  indexed_render_buffer Result = {};
  Result.NumItems = NumItems;
  Result.ItemSizeBytes = ItemSizeBytes;
  Result.TotalSizeBytes = ItemSizeBytes * NumItems;
  
  glGenVertexArrays(1, &Result.VAO);
  glBindVertexArray(Result.VAO);
  
  glGenBuffers(1, &Result.VBO);
  glBindBuffer(GL_ARRAY_BUFFER, Result.VBO);
  glBufferData(GL_ARRAY_BUFFER, Result.TotalSizeBytes, VertexData, GL_DYNAMIC_DRAW);
  
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  
  return(Result);
}

internal void IndexedRenderBufferDestroy(indexed_render_buffer *Buffer)
{
  glDeleteBuffers(1, &Buffer->VAO);
  glDeleteBuffers(1, &Buffer->VBO);
}

internal void IndexedRenderBufferSetAttrib(indexed_render_buffer *Buffer,
                                           u32 Index,
                                           u32 NumFloatVals,
                                           size_t AttribOffsetBytes)
{
  glBindVertexArray(Buffer->VAO);
  glBindBuffer(GL_ARRAY_BUFFER, Buffer->VBO);
  
  glEnableVertexAttribArray(Index);
  glVertexAttribPointer(Index, NumFloatVals, GL_FLOAT, GL_FALSE, Buffer->ItemSizeBytes, (void*)AttribOffsetBytes);
  
  // NOTE(eric): This is required for doing instanced rendering the way the we
  // want to do it. The default value is 0 causing attribute values to advance
  // once per vertex. This default behavior means that you cannot pass in the
  // values of multiple vertices and use gl_VertexID to select between them. By
  // setting it to 1 we instead cause it to advance once per instance (where an
  // instance has a number of vertices specified by the parameters to a
  // glDraw<Type>Instanced call). This allows us to pass along the data for
  // multiple vertices at a time and use `gl_VertexID` within our shaders to
  // select between these values.
  glVertexAttribDivisor(Index, 1);
  
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

///////////////////////////////////////////////////////////////////////////////
// renderer

internal void RendererCreate(platform_state *Platform, renderer *Renderer, shader_catalog *ShaderCatalog)
{
  Renderer->Extensions = (char*)glGetString(GL_EXTENSIONS);
  Renderer->ShaderCatalog = ShaderCatalog;
  OpenGLInit(Platform, Renderer->Extensions);
  
  // Initialize instanced rendering
  {
    // Lines
    {
      Renderer->LineBuffer = IndexedRenderBufferCreate(RENDERER_LINES_MAX, RENDERER_BYTES_PER_LINE, Renderer->LineInstanceData);
      
      IndexedRenderBufferSetAttrib(&Renderer->LineBuffer, 0, 2, 0); // StartPos (x,y)
      IndexedRenderBufferSetAttrib(&Renderer->LineBuffer, 1, 2, sizeof(v2)); // EndPos (x,y)
      IndexedRenderBufferSetAttrib(&Renderer->LineBuffer, 2, 4, 2*sizeof(v2)); // Color (r,g,b,a)
    }
    
    // Filled Rects
    {
      Renderer->FilledRectBuffer = IndexedRenderBufferCreate(RENDERER_FILLED_RECT_MAX, RENDERER_BYTES_PER_FILLED_RECT, Renderer->FilledRectInstanceData);
      
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 0, 2, 0); // V0 (x,y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 1, 2, sizeof(v2)); // V1 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 2, 2, 2*sizeof(v2)); // V2 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 3, 2, 3*sizeof(v2)); // V3 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 4, 4, 4*sizeof(v2)); // C0 (r, g, b, a)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 5, 4, 4*sizeof(v2)+sizeof(v4)); // C1 (r, g, b, a)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 6, 4, 4*sizeof(v2)+2*sizeof(v4)); // C2 (r, g, b, a)
      IndexedRenderBufferSetAttrib(&Renderer->FilledRectBuffer, 7, 4, 4*sizeof(v2)+3*sizeof(v4)); // C3 (r, g, b, a)
    }
    
    // Filled Circles
    {
      Renderer->FilledCircleBuffer = 
        IndexedRenderBufferCreate(RENDERER_FILLED_CIRCLE_MAX, RENDERER_BYTES_PER_FILLED_CIRCLE,
                                  Renderer->FilledCircleInstanceData);
      
      IndexedRenderBufferSetAttrib(&Renderer->FilledCircleBuffer, 0, 2, 0); // P0 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledCircleBuffer, 1, 2, sizeof(v2)); // P1 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledCircleBuffer, 2, 2, 2*sizeof(v2)); // P2 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledCircleBuffer, 3, 2, 3*sizeof(v2)); // P3 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->FilledCircleBuffer, 4, 3, 4*sizeof(v2)); // x,y,radius
      IndexedRenderBufferSetAttrib(&Renderer->FilledCircleBuffer, 5, 4, sizeof(v3) + 4*sizeof(v2)); // r,g,b,a 
    }
    
    // Textured Quads
    {
      Renderer->TexturedQuadBuffer = IndexedRenderBufferCreate(RENDERER_TEXTURED_QUADS_MAX, RENDERER_BYTES_PER_TEXTURED_QUAD, Renderer->TexturedQuadInstanceData);
      
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 0, 4, 0); // Source Rect (x, y, w, h)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 1, 2, sizeof(v4)); // V0 (x,y)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 2, 2, sizeof(v4)+sizeof(v2)); // V1 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 3, 2, sizeof(v4)+2*sizeof(v2)); // V2 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 4, 2, sizeof(v4)+3*sizeof(v2)); // V3 (x, y)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 5, 4, sizeof(v4)+4*sizeof(v2)); // C0 (r, g, b, a)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 6, 4, sizeof(v4)+4*sizeof(v2)+sizeof(v4)); // C1 (r, g, b, a)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 7, 4, sizeof(v4)+4*sizeof(v2)+2*sizeof(v4)); // C2 (r, g, b, a)
      IndexedRenderBufferSetAttrib(&Renderer->TexturedQuadBuffer, 8, 4, sizeof(v4)+4*sizeof(v2)+3*sizeof(v4)); // C3 (r, g, b, a)
    }
    
    // Text
    {
      Renderer->TextBuffer = IndexedRenderBufferCreate(RENDERER_TEXTS_MAX, RENDERER_BYTES_PER_TEXT, Renderer->TextInstanceData);
      
      IndexedRenderBufferSetAttrib(&Renderer->TextBuffer, 0, 4, 0); // vec4 = <vec2 Pos, vec2 UV>
      IndexedRenderBufferSetAttrib(&Renderer->TextBuffer, 1, 4, sizeof(v4)); // TextColor (r,g,b,a)
    }
  }
  
  glEnable(GL_BLEND);
  glEnable(GL_MULTISAMPLE);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

internal void RendererDestroy(renderer *Renderer)
{
  // Cleanup instanced rendering
  {
    IndexedRenderBufferDestroy(&Renderer->LineBuffer);
    IndexedRenderBufferDestroy(&Renderer->FilledRectBuffer);
    IndexedRenderBufferDestroy(&Renderer->FilledCircleBuffer);
    IndexedRenderBufferDestroy(&Renderer->TexturedQuadBuffer);
    IndexedRenderBufferDestroy(&Renderer->TextBuffer);
  }
}

internal void RendererBeginFrame(renderer *Renderer, platform_state* Platform, v2 Dim)
{
  // NOTE(eric): On library hot reload, the OpenGL function pointers
  // dynamically loaded from the library will be invalid. Reload them here.
  if (glIsProgram == NULL)
  {
    Renderer->Extensions = (char*)glGetString(GL_EXTENSIONS);
    OpenGLInit(Platform, Renderer->Extensions);
  }
  
  // Initialize render request
  {
    Renderer->NumRequests = 0;
    Renderer->ActiveRequest.Type = RENDER_REQUEST_null;
    Renderer->ActiveRequest.Flags = 0;
  }
  
  // Instanced rendering
  {
    Renderer->LineInstanceDataPos = 0;
    Renderer->FilledRectInstanceDataPos = 0;
    Renderer->FilledCircleInstanceDataPos = 0;
    Renderer->TexturedQuadInstanceDataPos = 0;
    Renderer->TextInstanceDataPos = 0;
  }

  // Clip stack
  {
    Renderer->ClipStackCount = 0;
    Renderer->ClipRect = V4(0, 0, Dim.Width, Dim.Height);
  }

  // MVP matrix stack
  {
    Renderer->MVPStackCount = 0;
    Renderer->MVPMatrix = Identity4x4();
  }
  
  Renderer->Dim = Dim;
}

// TODO: Merge this into RendererEndFrame once FXAA and Tone Mapping pass are also moved in.
internal void RendererFlush(renderer* Renderer)
{
  RendererFinishActiveRequest(Renderer);
  
  glEnable(GL_SCISSOR_TEST);
  glViewport(0, 0, (GLsizei)Renderer->Dim.Width, (GLsizei)Renderer->Dim.Height);
  glScissor(0, 0, (GLint)Renderer->Dim.Width, (GLint)Renderer->Dim.Height);

  m4x4 MVPMatrix = Identity4x4();
  
  foreach(I, Renderer->NumRequests)
  {
    render_request *Request = Renderer->Request + I;
    
    switch (Request->Type)
    {
      case RENDER_REQUEST_line:
      {
        // Upload data
        {
          glBindBuffer(GL_ARRAY_BUFFER, Renderer->LineBuffer.VBO);
          glBufferSubData(GL_ARRAY_BUFFER, 0, Request->DataSize, Renderer->LineInstanceData + Request->DataOffset);
          glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        
        u32 Shader = ShaderCatalogUse(Renderer->ShaderCatalog, "line");
        glBindVertexArray(Renderer->LineBuffer.VAO);
        {
          glUniformMatrix4fv(glGetUniformLocation(Shader, "ViewProjection"), 1, GL_FALSE, (f32*)MVPMatrix.E);
          
          GLint First = 0;
          GLsizei Count = 2;
          GLsizei InstanceCount = Request->DataSize / RENDERER_BYTES_PER_LINE;
          glDrawArraysInstanced(GL_LINES, First, Count, InstanceCount);
        }
        glBindVertexArray(0);
        // NOTE: Always run glUseProgram(0) when done with a shader, otherwise
        // when the next shader is used it will cause a recompilation penalty
        // due to GL state mismatch.
        glUseProgram(0);
      }
      break;
      case RENDER_REQUEST_filled_rect:
      {
        // Upload data
        {
          glBindBuffer(GL_ARRAY_BUFFER, Renderer->FilledRectBuffer.VBO);
          glBufferSubData(GL_ARRAY_BUFFER, 0, Request->DataSize, Renderer->FilledRectInstanceData + Request->DataOffset);
          glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        
        u32 Shader = ShaderCatalogUse(Renderer->ShaderCatalog, "filled_rect");
        glBindVertexArray(Renderer->FilledRectBuffer.VAO);
        {
          glUniformMatrix4fv(glGetUniformLocation(Shader, "ViewProjection"), 1, GL_FALSE, (f32*)MVPMatrix.E);
          
          GLint First = 0;
          GLsizei Count = 4;
          GLsizei InstanceCount = Request->DataSize / RENDERER_BYTES_PER_FILLED_RECT;
          glDrawArraysInstanced(GL_TRIANGLE_STRIP, First, Count, InstanceCount);
        }
        glBindVertexArray(0);
        glUseProgram(0);
      }
      break;
      case RENDER_REQUEST_filled_circle:
      {
        {
          glBindBuffer(GL_ARRAY_BUFFER, Renderer->FilledCircleBuffer.VBO);
          glBufferSubData(GL_ARRAY_BUFFER, 0, Request->DataSize,
                          Renderer->FilledCircleInstanceData + Request->DataOffset);
          glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        
        u32 Shader = ShaderCatalogUse(Renderer->ShaderCatalog, "filled_circle");
        glBindVertexArray(Renderer->FilledCircleBuffer.VAO);
        {
          glUniformMatrix4fv(glGetUniformLocation(Shader, "ViewProjection"), 1, GL_FALSE, (f32*)MVPMatrix.E);
          
          GLint First = 0;
          GLsizei Count = 4;
          GLsizei InstanceCount = Request->DataSize / RENDERER_BYTES_PER_FILLED_CIRCLE;
          glDrawArraysInstanced(GL_TRIANGLE_STRIP, First, Count, InstanceCount);
        }
        glBindVertexArray(0);
        glUseProgram(0);
      }
      break;
      case RENDER_REQUEST_textured_quad:
      {
        // Upload data
        {
          glBindBuffer(GL_ARRAY_BUFFER, Renderer->TexturedQuadBuffer.VBO);
          glBufferSubData(GL_ARRAY_BUFFER, 0, Request->DataSize, Renderer->TexturedQuadInstanceData + Request->DataOffset);
          glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        
        
        u32 Shader = ShaderCatalogUse(Renderer->ShaderCatalog, "textured_quad");
        glBindVertexArray(Renderer->TexturedQuadBuffer.VAO);
        {
          glUniformMatrix4fv(glGetUniformLocation(Shader, "ViewProjection"), 1, GL_FALSE, (f32*)MVPMatrix.E);
          
          
          glActiveTexture(GL_TEXTURE0 + Request->TexturedQuad.TextureID);
          glBindTexture(GL_TEXTURE_2D, Request->TexturedQuad.TextureID);
          // NOTE: For now, use GL_NEAREST to get a nice fat-pixel effect when scaling up.
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          
          glUniform1i(glGetUniformLocation(Shader, "Texture"), Request->TexturedQuad.TextureID);
          glUniform2f(glGetUniformLocation(Shader, "TextureDim"), Request->TexturedQuad.Dim.Width, Request->TexturedQuad.Dim.Height);
          
          GLint First = 0;
          GLsizei Count = 4;
          GLsizei InstanceCount = Request->DataSize / RENDERER_BYTES_PER_TEXTURED_QUAD;
          glDrawArraysInstanced(GL_TRIANGLE_STRIP, First, Count, InstanceCount);
        }
        glBindVertexArray(0);
        glUseProgram(0);
      }
      break;
      case RENDER_REQUEST_text:
      {
        // Upload data
        {
          glBindBuffer(GL_ARRAY_BUFFER, Renderer->TextBuffer.VBO);
          glBufferSubData(GL_ARRAY_BUFFER, 0, Request->DataSize, Renderer->TextInstanceData + Request->DataOffset);
          glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        
        // Draw
        u32 Shader = ShaderCatalogUse(Renderer->ShaderCatalog, "bitmap_font");
        glBindVertexArray(Renderer->TextBuffer.VAO);
        {
          glUniformMatrix4fv(glGetUniformLocation(Shader, "ViewProjection"), 1, GL_FALSE, (f32*)MVPMatrix.E);
          
          glActiveTexture(GL_TEXTURE0 + Request->Text.TextureID);
          glBindTexture(GL_TEXTURE_2D, Request->Text.TextureID);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          
          glUniform1i(glGetUniformLocation(Shader, "Texture"), Request->Text.TextureID);
          
          GLint First = 0;
          GLsizei Count = 4;
          GLsizei InstanceCount = Request->DataSize / RENDERER_BYTES_PER_TEXT;
          glDrawArraysInstanced(GL_TRIANGLE_STRIP, First, Count, InstanceCount);
        }
        glBindVertexArray(0);
        glUseProgram(0);
      }
      break;
      case RENDER_REQUEST_set_clip:
      {
        glScissor(
          Request->Clip.Rect.X,
          Request->Clip.Rect.Y,
          (GLint)Request->Clip.Rect.Width,
          (GLint)Request->Clip.Rect.Height
        );
      }
      break;
      case RENDER_REQUEST_set_mvp_matrix:
      {
        MVPMatrix = Request->MVPMatrix.MVP;
      }
      break;
      default:
      {
        fprintf(stderr, "error: unknown render command %d\n", Request->Type);
      }
      break;
    }
  }

  // Reset render data in case additional commands are issued. This avoids
  // double-rendering data or overflowing render buffers.
  {
    Renderer->NumRequests = 0;
    Renderer->ActiveRequest.Type = RENDER_REQUEST_null;
    Renderer->ActiveRequest.Flags = 0;
  }
  
  {
    Renderer->LineInstanceDataPos = 0;
    Renderer->FilledRectInstanceDataPos = 0;
    Renderer->FilledCircleInstanceDataPos = 0;
    Renderer->TexturedQuadInstanceDataPos = 0;
    Renderer->TextInstanceDataPos = 0;
  }

  {
    Renderer->ClipStackCount = 0;
    Renderer->ClipRect = V4(0, 0, Renderer->Dim.Width, Renderer->Dim.Height);
  }

  {
    Renderer->MVPStackCount = 0;
    Renderer->MVPMatrix = Identity4x4();
  }
}

internal void RendererEndFrame(renderer *Renderer)
{
  //RendererFlush(Renderer);

  GLenum Error = glGetError();
  if (Error != GL_NO_ERROR)
  {
    fprintf(stderr, "\n[RendererEndFrame] OpenGL Error %i: %s\n", (int)Error, gluErrorString(Error));
  }
}

internal void RendererClear(renderer *Renderer, v4 ClearColor)
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(ClearColor.R, ClearColor.G, ClearColor.B, ClearColor.A);
}

internal void RendererSetTarget(renderer *Renderer, framebuffer *Target)
{
  FramebufferMaybeResize(Target, Renderer->Dim);
  glBindFramebuffer(GL_FRAMEBUFFER, Target->FBO);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

internal void RendererClearTarget(renderer *Renderer)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

internal void RendererFinishActiveRequest(renderer *Renderer)
{
  if (Renderer->ActiveRequest.Type != RENDER_REQUEST_null)
  {
    Assert(Renderer->NumRequests < RENDERER_REQUESTS_MAX);
    Renderer->Request[Renderer->NumRequests++] = Renderer->ActiveRequest;
    Renderer->ActiveRequest.Type = RENDER_REQUEST_null;
  }
}

internal void RendererPushLine(renderer *Renderer, u32 Flags, v2 Start, v2 End, v4 Color)
{
  Assert(Renderer->LineInstanceDataPos + RENDERER_BYTES_PER_LINE <= sizeof(Renderer->LineInstanceData));
  render_request_type RequestType = RENDER_REQUEST_line;
  
  if (Renderer->ActiveRequest.Type != RequestType || Renderer->ActiveRequest.Flags != Flags)
  {
    RendererFinishActiveRequest(Renderer);
    Renderer->ActiveRequest.Type = RequestType;
    Renderer->ActiveRequest.Flags = Flags;
    Renderer->ActiveRequest.DataOffset = Renderer->LineInstanceDataPos;
    Renderer->ActiveRequest.DataSize = RENDERER_BYTES_PER_LINE;
  }
  else
  {
    Renderer->ActiveRequest.DataSize += RENDERER_BYTES_PER_LINE;
  }
  
  f32 *Data = (f32*)(Renderer->LineInstanceData + Renderer->LineInstanceDataPos);
  Data[0] = Start.X;
  Data[1] = Start.Y;
  Data[2] = End.X;
  Data[3] = End.Y;
  Data[4] = Color.R;
  Data[5] = Color.G;
  Data[6] = Color.B;
  Data[7] = Color.A;
  Renderer->LineInstanceDataPos += RENDERER_BYTES_PER_LINE;
}

internal void RendererPushFilledRect(renderer *Renderer, u32 Flags, v4 Rect, v4 Color)
{
  Assert(Renderer->FilledRectInstanceDataPos + RENDERER_BYTES_PER_FILLED_RECT <= sizeof(Renderer->FilledRectInstanceData));
  render_request_type RequestType = RENDER_REQUEST_filled_rect;
  
  if (Renderer->ActiveRequest.Type != RequestType || Renderer->ActiveRequest.Flags != Flags)
  {
    RendererFinishActiveRequest(Renderer);
    Renderer->ActiveRequest.Type = RequestType;
    Renderer->ActiveRequest.Flags = Flags;
    Renderer->ActiveRequest.DataOffset = Renderer->FilledRectInstanceDataPos;
    Renderer->ActiveRequest.DataSize = RENDERER_BYTES_PER_FILLED_RECT;
  }
  else
  {
    Renderer->ActiveRequest.DataSize += RENDERER_BYTES_PER_FILLED_RECT;
  }
  
  f32 *Data = (f32*)(Renderer->FilledRectInstanceData + Renderer->FilledRectInstanceDataPos);
  Data[0] = Rect.X;
  Data[1] = Rect.Y + Rect.Height;
  Data[2] = Rect.X;
  Data[3] = Rect.Y;
  Data[4] = Rect.X + Rect.Width;
  Data[5] = Rect.Y + Rect.Height;
  Data[6] = Rect.X + Rect.Width;
  Data[7] = Rect.Y;
  Data[8] = Color.R;
  Data[9] = Color.G;
  Data[10] = Color.B;
  Data[11] = Color.A;
  Data[12] = Color.R;
  Data[13] = Color.G;
  Data[14] = Color.B;
  Data[15] = Color.A;
  Data[16] = Color.R;
  Data[17] = Color.G;
  Data[18] = Color.B;
  Data[19] = Color.A;
  Data[20] = Color.R;
  Data[21] = Color.G;
  Data[22] = Color.B;
  Data[23] = Color.A;
  Renderer->FilledRectInstanceDataPos += RENDERER_BYTES_PER_FILLED_RECT;
}

internal void RendererPushFilledCircle(renderer *Renderer, u32 Flags, v2 Center, f32 Radius, v4 Color) {
  Assert(Renderer->FilledCircleInstanceDataPos + RENDERER_BYTES_PER_FILLED_CIRCLE <= sizeof(Renderer->FilledCircleInstanceData));
  render_request_type RequestType = RENDER_REQUEST_filled_circle;
  
  if (Renderer->ActiveRequest.Type != RequestType || Renderer->ActiveRequest.Flags != Flags)
  {
    RendererFinishActiveRequest(Renderer);
    Renderer->ActiveRequest.Type = RequestType;
    Renderer->ActiveRequest.Flags = Flags;
    Renderer->ActiveRequest.DataOffset = Renderer->FilledCircleInstanceDataPos;
    Renderer->ActiveRequest.DataSize = RENDERER_BYTES_PER_FILLED_CIRCLE;
  }
  else
  {
    Renderer->ActiveRequest.DataSize += RENDERER_BYTES_PER_FILLED_CIRCLE;
  }
  
  f32 *Data = (f32*)(Renderer->FilledCircleInstanceData + Renderer->FilledCircleInstanceDataPos);
  Data[0] = Center.X - Radius;
  Data[1] = Center.Y + Radius;
  Data[2] = Center.X - Radius;
  Data[3] = Center.Y - Radius;
  Data[4] = Center.X + Radius;
  Data[5] = Center.Y + Radius;
  Data[6] = Center.X + Radius;
  Data[7] = Center.Y - Radius;
  Data[8] = Center.X;
  Data[9] = Center.Y;
  Data[10] = Radius;
  Data[11] = Color.R;
  Data[12] = Color.G;
  Data[13] = Color.B;
  Data[14] = Color.A;
  Renderer->FilledCircleInstanceDataPos += RENDERER_BYTES_PER_FILLED_CIRCLE;
}

internal void RendererPushTexturedQuad(renderer *Renderer, u32 Flags, GLuint TextureID, v2 TextureDim, v4 SourceRect, v4 DestRect, v4 Color)
{
  Assert(Renderer->TexturedQuadInstanceDataPos + RENDERER_BYTES_PER_TEXTURED_QUAD <= sizeof(Renderer->TexturedQuadInstanceData));
  render_request_type RequestType = RENDER_REQUEST_textured_quad;
  
  if (Renderer->ActiveRequest.Type != RequestType || 
      Renderer->ActiveRequest.Flags != Flags ||
      Renderer->ActiveRequest.TexturedQuad.TextureID != TextureID)
  {
    RendererFinishActiveRequest(Renderer);
    Renderer->ActiveRequest.Type = RequestType;
    Renderer->ActiveRequest.Flags = Flags;
    Renderer->ActiveRequest.DataOffset = Renderer->TexturedQuadInstanceDataPos;
    Renderer->ActiveRequest.DataSize = RENDERER_BYTES_PER_TEXTURED_QUAD;
    Renderer->ActiveRequest.TexturedQuad.TextureID = TextureID;
    Renderer->ActiveRequest.TexturedQuad.Dim = TextureDim;
  }
  else
  {
    Renderer->ActiveRequest.DataSize += RENDERER_BYTES_PER_TEXTURED_QUAD;
  }
  
  f32 *Data = (f32*)(Renderer->TexturedQuadInstanceData + Renderer->TexturedQuadInstanceDataPos);
  Data[0] = SourceRect.X;
  Data[1] = SourceRect.Y;
  Data[2] = SourceRect.Width;
  Data[3] = SourceRect.Height;
  Data[4] = DestRect.X;
  Data[5] = DestRect.Y;
  Data[6] = DestRect.X;
  Data[7] = DestRect.Y + DestRect.Height;
  Data[8] = DestRect.X + DestRect.Width;
  Data[9] = DestRect.Y;
  Data[10] = DestRect.X + DestRect.Width;
  Data[11] = DestRect.Y + DestRect.Height;
  Data[12] = Color.R;
  Data[13] = Color.G;
  Data[14] = Color.B;
  Data[15] = Color.A;
  Data[16] = Color.R;
  Data[17] = Color.G;
  Data[18] = Color.B;
  Data[19] = Color.A;
  Data[20] = Color.R;
  Data[21] = Color.G;
  Data[22] = Color.B;
  Data[23] = Color.A;
  Data[24] = Color.R;
  Data[25] = Color.G;
  Data[26] = Color.B;
  Data[27] = Color.A;
  Renderer->TexturedQuadInstanceDataPos += RENDERER_BYTES_PER_TEXTURED_QUAD;
}

internal void RendererPushTexture(renderer *Renderer, u32 Flags, texture Texture, v4 SourceRect, v4 DestRect, v4 Color) {
  if (Texture.Loaded) {
    RendererPushTexturedQuad(Renderer,
                             Flags,
                             Texture.ID,
                             Texture.Dim,
                             SourceRect,
                             DestRect,
                             Color);
  }
}

internal void RendererPushTextChar(renderer *Renderer, u32 Flags, GLuint TextureID, v2 Pos, v2 Dim, v4 Color) {
  Assert(Renderer->TextInstanceDataPos + RENDERER_BYTES_PER_TEXT <= sizeof(Renderer->TextInstanceData));
  render_request_type RequestType = RENDER_REQUEST_text;
  
  if (Renderer->ActiveRequest.Type != RequestType || 
      Renderer->ActiveRequest.Flags != Flags ||
      Renderer->ActiveRequest.Text.TextureID != TextureID)
  {
    RendererFinishActiveRequest(Renderer);
    Renderer->ActiveRequest.Type = RequestType;
    Renderer->ActiveRequest.Flags = Flags;
    Renderer->ActiveRequest.DataOffset = Renderer->TextInstanceDataPos;
    Renderer->ActiveRequest.DataSize = RENDERER_BYTES_PER_TEXT;
    Renderer->ActiveRequest.Text.TextureID = TextureID;
  }
  else
  {
    Renderer->ActiveRequest.DataSize += RENDERER_BYTES_PER_TEXT;
  }
  
  f32 *Data = (f32*)(Renderer->TextInstanceData + Renderer->TextInstanceDataPos);
  Data[0] = Pos.X;
  Data[1] = Pos.Y;
  Data[2] = Dim.Width;
  Data[3] = Dim.Height;
  Data[4] = Color.R;
  Data[5] = Color.G;
  Data[6] = Color.B;
  Data[7] = Color.A;
  Renderer->TextInstanceDataPos += RENDERER_BYTES_PER_TEXT;
}

internal void RendererPushText(renderer *Renderer, u32 Flags, font *Font, const char *Text, v2 Pos, v4 Color) {
  v2 NextPos = Pos;
  char *NextCh = (char*)Text;
  u32 PreviousGlyph = 0;
  while (*NextCh) {
    font_glyph_cache *Cached = Font->GlyphCache + (u32)(*NextCh);

    f32 Scale = 1.0;
    f32 XPos = NextPos.X + Cached->Bearing.X * Scale;
    f32 YPos = NextPos.Y - (Cached->Dim.Y - Cached->Bearing.Y) * Scale;
    
    f32 Width = Cached->Dim.Width * Scale;
    f32 Height = Cached->Dim.Height * Scale;

    if (*NextCh == '\t') {
      Cached = Font->GlyphCache + (u32)' ';
      Width = Cached->Dim.Width * 4.0f;
      Height = Cached->Dim.Height;
    }
    
    u32 GlyphIndex = FT_Get_Char_Index(Font->Face, *NextCh);
    if (PreviousGlyph)
    {
      FT_Vector Delta;
      FT_Get_Kerning(Font->Face, PreviousGlyph, GlyphIndex, FT_KERNING_DEFAULT, &Delta);
      XPos += (Delta.x >> 6);
    }

    RendererPushTextChar(Renderer,
                         Flags,
                         Font->GlyphTextures[(u32)*NextCh],
                         V2(XPos, YPos),
                         V2(Width, Height),
                         Color);

    if (*NextCh == '\t') {
      NextPos.X += (Cached->Advance >> 6) * 4;
    } else {
      NextPos.X += (Cached->Advance >> 6) * Scale;
    }

    PreviousGlyph = GlyphIndex;
    
    ++NextCh;
  }
}

internal void RendererPushClip(renderer *Renderer, v4 ClipRect)
{
  RendererFinishActiveRequest(Renderer);

  Assert(Renderer->ClipStackCount < RENDERER_CLIP_STACK_MAX);
  Renderer->ClipStack[Renderer->ClipStackCount++] = Renderer->ClipRect;
  Renderer->ClipRect = ClipRect;

  Assert(Renderer->NumRequests < RENDERER_REQUESTS_MAX);
  render_request ClipRequest = {};
  ClipRequest.Type = RENDER_REQUEST_set_clip;
  ClipRequest.Clip.Rect = ClipRect;
  Renderer->Request[Renderer->NumRequests++] = ClipRequest;
}

internal void RendererPopClip(renderer *Renderer)
{
  RendererFinishActiveRequest(Renderer);
  Assert(Renderer->ClipStackCount > 0);

  --Renderer->ClipStackCount;
  Renderer->ClipRect = Renderer->ClipStack[Renderer->ClipStackCount];

  Assert(Renderer->NumRequests < RENDERER_REQUESTS_MAX);
  render_request ClipRequest = {};
  ClipRequest.Type = RENDER_REQUEST_set_clip;
  ClipRequest.Clip.Rect = Renderer->ClipRect;
  Renderer->Request[Renderer->NumRequests++] = ClipRequest;
}

internal void RendererPushMVPMatrix(renderer *Renderer, m4x4 MVP)
{
  RendererFinishActiveRequest(Renderer);
  Assert(Renderer->MVPStackCount < RENDERER_MVP_MATRIX_STACK_MAX);
  Renderer->MVPStack[Renderer->MVPStackCount++] = Renderer->MVPMatrix;
  Renderer->MVPMatrix = MVP;

  Assert(Renderer->NumRequests < RENDERER_REQUESTS_MAX);
  render_request MVPRequest = {};
  MVPRequest.Type = RENDER_REQUEST_set_mvp_matrix;
  MVPRequest.MVPMatrix.MVP = MVP;
  Renderer->Request[Renderer->NumRequests++] = MVPRequest;
}

internal void RendererPopMVPMatrix(renderer *Renderer)
{
  RendererFinishActiveRequest(Renderer);
  Assert(Renderer->MVPStackCount > 0);
  --Renderer->MVPStackCount;
  Renderer->MVPMatrix = Renderer->MVPStack[Renderer->MVPStackCount];

  Assert(Renderer->NumRequests < RENDERER_REQUESTS_MAX);
  render_request MVPRequest = {};
  MVPRequest.Type = RENDER_REQUEST_set_mvp_matrix;
  MVPRequest.MVPMatrix.MVP = Renderer->MVPMatrix;
  Renderer->Request[Renderer->NumRequests++] = MVPRequest;
}

internal void Renderer2DRightHanded(renderer *Renderer, v2 Dim)
{
  RendererPushMVPMatrix(
    Renderer,
    Orthographic(0, Dim.Width, 0, Dim.Height, 0, 1)
  );
}

///////////////////////////////////////////////////////////////////////////////

const glenum_to_string DebugMessageSourceString[] = {
  { .Key = GL_DEBUG_SOURCE_API, .Value = "API" },
  { .Key = GL_DEBUG_SOURCE_WINDOW_SYSTEM, .Value = "Window System" },
  { .Key = GL_DEBUG_SOURCE_SHADER_COMPILER, .Value = "Shader Compiler" },
  { .Key = GL_DEBUG_SOURCE_THIRD_PARTY, .Value = "Third Party" },
  { .Key = GL_DEBUG_SOURCE_APPLICATION, .Value = "Application" },
};

const glenum_to_string DebugMessageTypeString[] = {
  { .Key = GL_DEBUG_TYPE_ERROR, .Value = "Error" },
  { .Key = GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, .Value = "Deprecated Behavior" },
  { .Key = GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, .Value = "Undefined Behavior" },
  { .Key = GL_DEBUG_TYPE_PORTABILITY, .Value = "Portability" },
  { .Key = GL_DEBUG_TYPE_PERFORMANCE, .Value = "Performance" },
  { .Key = GL_DEBUG_TYPE_MARKER, .Value = "Marker" },
  { .Key = GL_DEBUG_TYPE_PUSH_GROUP, .Value = "Push Group" },
  { .Key = GL_DEBUG_TYPE_POP_GROUP, .Value = "Pop Group" },
  { .Key = GL_DEBUG_TYPE_OTHER, .Value = "Other" },
};

const glenum_to_string DebugMessageSeverityString[] = {
  { .Key = GL_DEBUG_SEVERITY_LOW, .Value = "Low" },
  { .Key = GL_DEBUG_SEVERITY_MEDIUM, .Value = "Medium" },
  { .Key = GL_DEBUG_SEVERITY_HIGH, "High" },
  { .Key = GL_DEBUG_SEVERITY_NOTIFICATION, "Notification" },
};

internal const char* GetEnumValue(glenum_to_string *Enum, u32 EnumSize, GLenum Key)
{
  for (u32 I = 0; I < EnumSize; ++I)
  {
    if (Enum[I].Key == Key)
    {
      return(Enum[I].Value);
    }
  }
  
  return "Bad Key";
}

#define GLEnumName(Array, Key) GetEnumValue((glenum_to_string*)Array, ArrayCount(Array), Key)

internal void OpenGLDebugMessageCallback(
                                         GLenum Source,
                                         GLenum Type,
                                         GLuint ID,
                                         GLenum Severity,
                                         GLsizei MessageLength,
                                         const GLchar *Message,
                                         const void *UserParam
                                         )
{
  fprintf(stderr, "[%s.%s.%d %s]: %s\n",
          GLEnumName(DebugMessageSourceString, Source),
          GLEnumName(DebugMessageTypeString, Type),
          ID,
          GLEnumName(DebugMessageSeverityString, Severity),
          Message);
}

///////////////////////////////////////////////////////////////////////////////

internal void OpenGLInit(platform_state *Platform, const char *ExtensionList)
{
  OpenGLLoadProcedures(Platform, ExtensionList);
  
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(OpenGLDebugMessageCallback, NULL);
}

internal void OpenGLLoadProcedures(platform_state *Platform, const char *ExtensionList)
{
#define GLProc(Type, Name) gl##Name = (PFNGL##Type##PROC)Platform->Interface.GetOpenGLProcAddress("gl" #Name);
#include "opengl_procedure_list.h"
  
  // Versioned extensions
  {
    GLint Major, Minor;
    glGetIntegerv(GL_MAJOR_VERSION, &Major);
    glGetIntegerv(GL_MINOR_VERSION, &Minor);
    if (Major > 4 || (Major == 4 && Minor >= 5) || ExtensionInList(ExtensionList, "GL_ARB_clip_control"))
    {
      // NOTE(eric): This is here as a stub for when/if we move to 3D
      // applications.
      // 
      // We want to reverse the Z values used in the depth buffer as well as
      // change the range from OpenGL's default [-1, 1] range to the more
      // sensible [0, 1] range. This increases the precision of the depth
      // buffer. This is very useful in general, but especially for 3D games
      // with long view distances. For more info see:
      //
      // https://developer.nvidia.com/content/depth-precision-visualized
      glClipControl = (PFNGLCLIPCONTROLPROC)Platform->Interface.GetOpenGLProcAddress("glClipControl");
      //glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    }
  }
}
