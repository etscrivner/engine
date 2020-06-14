#include "shaders.h"
#include "common/memory_arena.h"

internal GLuint GLCompileShader(platform_state *Platform, scoped_arena *ScopedArena, char *ShaderSource, GLenum ShaderType);
internal GLuint GLLinkShaders(scoped_arena *ScopedArena, platform_state *Platform, GLuint VertexShader, GLuint FragmentShader);
internal GLuint GLCompileAndLinkShaders(scoped_arena *ScopedArena, platform_state *Platform, char *ShaderSource);

///////////////////////////////////////////////////////////////////////////////

internal void LoadAndCompileShaders(platform_state *Platform, scoped_arena *ScopedArena, shader *Shader)
{
  platform_entire_file ShaderFile;
  if (!Platform->Interface.LoadEntireFile(Shader->Internal.ShaderFileName, &ShaderFile))
  {
    Platform->Interface.Log("error: failed to load vertex shader '%s'\n", Shader->Internal.ShaderFileName);
  }

  Shader->Program = GLCompileAndLinkShaders(ScopedArena, Platform, (char*)ShaderFile.Data);

  Platform->Interface.FreeEntireFile(&ShaderFile);

  if (Shader->Program == 0)
  {
    Platform->Interface.Log(
      "error: failed to load shader:'%s'\n",
      Shader->Internal.ShaderFileName
    );
  }
  else
  {
    Platform->Interface.Log(
      "info: successfully loaded shader:'%s'\n",
      Shader->Internal.ShaderFileName
    );
  }
}

internal void ShaderLoad(shader *Shader, platform_state *Platform, game_state *GameState, char *ShaderFile)
{
  scoped_arena ScopedArena(&GameState->TransientArena);

  // TODO: @Cleanup: @MemoryLeak: Figure out how to not have to keep pushing
  // this name onto the arena if we can avoid it. If we ever start
  // loading/unloading shaders then this creates a problem since the arena
  // doesn't provide a great way to release data from random areas. Ideas:
  //    - String storage.
  //    - Fixed max on string size in buffer these are copied into.
  strncpy(Shader->Internal.ShaderFileName, ShaderFile, 128);
  // Result.Internal.ShaderFileName = ArenaPushArray(&GameState->PermanentArena, strlen(ShaderFile)+1, char);
  // strcpy(Result.Internal.ShaderFileName, ShaderFile);

  Shader->Internal.ShaderFile = WatchedFile(Shader->Internal.ShaderFileName);
  if (WatchedFileHasError(&Shader->Internal.ShaderFile))
  {
    Platform->Interface.Log(
      "error: unable to watch file '%s': %s\n",
      ShaderFile,
      WatchedFileGetError(&Shader->Internal.ShaderFile)
    );
  }

  LoadAndCompileShaders(Platform, &ScopedArena, Shader);
}

internal void ShaderDestroy(shader *Shader)
{
  if (glIsProgram(Shader->Program) != GL_TRUE)
    glDeleteShader(Shader->Program);
}

internal void ShaderUse(shader *Shader)
{
  if (glIsProgram(Shader->Program) != GL_TRUE)
    return;

  glValidateProgram(Shader->Program);
  glUseProgram(Shader->Program);
}

internal void ShaderHotLoad(platform_state *Platform, memory_arena *TransientArena, shader *Shader)
{
  scoped_arena ScopedArena(TransientArena);

  const char *ShaderFileName = Shader->Internal.ShaderFile.FilePath;

  WatchedFileUpdate(&Shader->Internal.ShaderFile);
  if (WatchedFileHasError(&Shader->Internal.ShaderFile))
  {
    Platform->Interface.Log(
      "error: unable to watch file %s: %s\n",
      ShaderFileName,
      WatchedFileGetError(&Shader->Internal.ShaderFile)
    );
  }

  // Need to recompile both if either changes
  if (Shader->Internal.ShaderFile.WasModified)
  {
    LoadAndCompileShaders(Platform, &ScopedArena, Shader);
  }
}

///////////////////////////////////////////////////////////////////////////////

internal GLuint GLCompileShader(scoped_arena *ScopedArena, platform_state *Platform, char *ShaderSource, GLenum ShaderType)
{
  local_persist char VertexShaderPreamble[] = R"END(
#version 330 core
#define VERTEX_SHADER                       
  )END";
  local_persist char FragmentShaderPreamble[] = R"END(
#version 330 core
#define FRAGMENT_SHADER
  )END";

  GLuint Result = glCreateShader(ShaderType);
  if (Result != 0)
  {
    char *ShaderPreamble = (ShaderType == GL_VERTEX_SHADER) ? VertexShaderPreamble : FragmentShaderPreamble;
    const char *ShaderStrings[2] = { ShaderPreamble, ShaderSource };
    const GLint ShaderStringLengths[2] = { (GLint)strlen(ShaderPreamble), (GLint)strlen(ShaderSource) };

    glShaderSource(Result, 2, ShaderStrings, ShaderStringLengths);
    glCompileShader(Result);
  }
  else
  {
    GLint ShaderCompileStatus = GL_FALSE;
    glGetShaderiv(Result, GL_COMPILE_STATUS, &ShaderCompileStatus);
    if (ShaderCompileStatus != GL_TRUE)
    {
      GLint ShaderErrorLogLength = 0;
      glGetShaderiv(Result, GL_INFO_LOG_LENGTH, &ShaderErrorLogLength);
      if (ShaderErrorLogLength > 0)
      {
        char *ErrorLog = ScopedArenaPushArray(ScopedArena, ShaderErrorLogLength, char);
        glGetShaderInfoLog(Result, ShaderErrorLogLength, &ShaderErrorLogLength, ErrorLog);
        Platform->Interface.Log("error: shader compilation failed:\n%s\n", ErrorLog);
      }

      glDeleteShader(Result);
      Result = 0;
    }
  }

  return(Result);
}

internal GLuint GLLinkShaders(scoped_arena *ScopedArena, platform_state *Platform, GLuint VertexShader, GLuint FragmentShader)
{
  GLuint Program = glCreateProgram();
  if (Program != 0)
  {
    glAttachShader(Program, VertexShader);
    glAttachShader(Program, FragmentShader);
    glLinkProgram(Program);

    GLint LinkStatus = GL_FALSE;
    glGetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
    if (LinkStatus != GL_TRUE)
    {
      GLint LinkerErrorLogLength = 0;
      glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &LinkerErrorLogLength);
      if (LinkerErrorLogLength > 0)
      {
        char *ErrorLog = ScopedArenaPushArray(ScopedArena, LinkerErrorLogLength, char);
        glGetProgramInfoLog(Program, LinkerErrorLogLength, &LinkerErrorLogLength, ErrorLog);
        Platform->Interface.Log("error: shader linking failed:\n%s\n", ErrorLog);
      }

      glDeleteProgram(Program);
      Program = 0;
    }
  }

  return(Program);
}

internal GLuint GLCompileAndLinkShaders(scoped_arena *ScopedArena,
                                        platform_state *Platform,
                                        char *ShaderSource)
{
  GLuint Program = 0;

  GLuint VS = GLCompileShader(ScopedArena, Platform, ShaderSource, GL_VERTEX_SHADER);
  if (VS != 0)
  {
    GLuint FS = GLCompileShader(ScopedArena, Platform, ShaderSource, GL_FRAGMENT_SHADER);
    if (FS != 0)
    {
      Program = GLLinkShaders(ScopedArena, Platform, VS, FS);
      glDeleteShader(FS);
    }

    glDeleteShader(VS);
  }

  return(Program);
}
