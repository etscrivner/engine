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
    {
      GLuint GLFormat = (Format == FRAMEBUFFER_TEXTURE_FORMAT_hdr) ? GL_RGBA16F : GL_RGBA;
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
    Assert(FramebufferIsValid(Framebuffer));
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

internal void FramebufferBindToTexture(framebuffer *Framebuffer, GLuint Texture)
{
  glActiveTexture(Texture);
  glBindTexture(GL_TEXTURE_2D, Framebuffer->TextureAttachment);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

///////////////////////////////////////////////////////////////////////////////
// renderer

internal void RendererCreate(memory_arena *TransientArena, platform_state *Platform, renderer *Renderer)
{
  Renderer->Extensions = (char*)glGetString(GL_EXTENSIONS);
  OpenGLInit(Platform, Renderer->Extensions);

  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

internal void RendererDestroy(renderer *Renderer)
{
  
}

internal void RendererBeginFrame(renderer *Renderer, platform_state* Platform, v2 Dim, v4 ClearColor)
{
  // NOTE(eric): On library hot reload, the OpenGL function pointers
  // dynamically loaded from the library will be invalid. Reload them here.
  if (glIsProgram == NULL)
  {
    Renderer->Extensions = (char*)glGetString(GL_EXTENSIONS);
    OpenGLInit(Platform, Renderer->Extensions);
  }

  Renderer->Dim = Dim;
  Renderer->ClearColor = ClearColor;
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(Renderer->ClearColor.R, Renderer->ClearColor.G, Renderer->ClearColor.B, Renderer->ClearColor.A);
}

internal void RendererEndFrame(renderer *Renderer)
{
  GLenum Error = glGetError();
  if (Error != GL_NO_ERROR)
  {
    fprintf(stderr, "\n[RendererEndFrame] OpenGL Error %i: %s\n", (int)Error, gluErrorString(Error));
  }
}

internal void RendererSetTarget(renderer *Renderer, framebuffer *Target)
{
  FramebufferMaybeResize(Target, Renderer->Dim);
  glBindFramebuffer(GL_FRAMEBUFFER, Target->FBO);
  //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

internal void RendererClearTarget(renderer *Renderer)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
  fprintf(
    stderr, "[%s - %s - %d (Sev: %s)]:\n%s\n",
    GLEnumName(DebugMessageSourceString, Source),
    GLEnumName(DebugMessageTypeString, Type),
    ID,
    GLEnumName(DebugMessageSeverityString, Severity),
    Message
  );
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
