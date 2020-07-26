#include "shaders.h"
#include "common/memory_arena.h"

internal GLuint GLCompileShader(platform_state *Platform, scoped_arena *ScopedArena, char *ShaderSource, GLenum ShaderType);
internal GLuint GLLinkShaders(scoped_arena *ScopedArena, platform_state *Platform, GLuint VertexShader, GLuint FragmentShader);
internal GLuint GLCompileAndLinkShaders(scoped_arena *ScopedArena, platform_state *Platform, char *ShaderSource);

///////////////////////////////////////////////////////////////////////////////

internal b32 ShaderCatalogInit(shader_catalog *Catalog, memory_arena *TransientArena)
{
  Catalog->TransientArena = TransientArena;
  Catalog->NumEntries = 0;
  return WatchedFileSetCreate(&Catalog->Watcher);
}

internal void ShaderCatalogDestroy(shader_catalog *Catalog)
{
  foreach(I, Catalog->NumEntries)
  {
    shader_catalog_entry *Entry = Catalog->Entry + I;
    if (glIsProgram(Entry->Program) != GL_TRUE)
    {
      glDeleteShader(Entry->Program);
    }
  }
}

internal b32 ShaderCatalogAdd(shader_catalog *Catalog, platform_state *Platform, char *ShaderFile, char *ReferenceName)
{
  Assert(Catalog->NumEntries < SHADER_CATALOG_MAX_SHADERS);
  b32 Result = true;
  shader_catalog_entry *Entry = Catalog->Entry + Catalog->NumEntries++;
  
  // Copy the reference name into the shader entry
  strncpy(Entry->ReferenceName, ReferenceName, SHADER_CATALOG_REFERENCE_NAME_MAX_SIZE);
  
  // Load the shader and store it in the reference entry
  platform_entire_file File;
  if (Platform->Interface.LoadEntireFile(ShaderFile, &File)) {
    scoped_arena ScopedArena(Catalog->TransientArena);
    Entry->Program = GLCompileAndLinkShaders(&ScopedArena, Platform, (char*)File.Data);
    Platform->Interface.FreeEntireFile(&File);
    
    if (Entry->Program == 0) {
      Platform->Interface.Log("error: failed to load shader: '%s'\n", ShaderFile);
      Result = false;
    } else {
      Platform->Interface.Log("info: successfully loaded shader: '%s' (%d)\n", ShaderFile, Entry->Program);
      Entry->WatcherHandle = WatchedFileSetAdd(&Catalog->Watcher, ShaderFile);
      if (Entry->WatcherHandle == -1) {
        Platform->Interface.Log("error: failed to watch shader '%s'\n", ShaderFile);
        Result = false;
      } else {
        Result = true;
      }
    }
  } else {
    Platform->Interface.Log("error: failed to load shader '%s\n", ShaderFile);
    Result = false;
  }
  
  return(Result);
}

internal GLuint ShaderCatalogGet(shader_catalog *Catalog, char *ReferenceName)
{
  GLuint Result = 0;
  foreach(I, Catalog->NumEntries)
  {
    shader_catalog_entry *Entry = Catalog->Entry + I;
    if (strncmp(Entry->ReferenceName, ReferenceName, SHADER_CATALOG_REFERENCE_NAME_MAX_SIZE) == 0)
    {
      Result = Entry->Program;
      break;
    }
  }
  
  return(Result);
}

internal GLuint ShaderCatalogUse(shader_catalog *Catalog, char *ReferenceName)
{
  GLuint Result = 0;
  foreach(I, Catalog->NumEntries)
  {
    shader_catalog_entry *Entry = Catalog->Entry + I;
    if (strncmp(Entry->ReferenceName, ReferenceName, SHADER_CATALOG_REFERENCE_NAME_MAX_SIZE) == 0)
    {
      if (glIsProgram(Entry->Program) != GL_TRUE)
      {
        break;
      }
      
      glValidateProgram(Entry->Program);
      glUseProgram(Entry->Program);
      
      Result = Entry->Program;
      break;
    }
  }
  
  return(Result);
}

internal b32 ShaderCatalogUpdate(shader_catalog *Catalog, platform_state *Platform)
{
  b32 Result = false;
  
  watched_file_iter Iter = WatchedFileSetUpdate(&Catalog->Watcher);
  while (IsValid(Iter)) {
    platform_entire_file File;
    if (Platform->Interface.LoadEntireFile(Iter.FileName, &File)) {
      foreach(I, Catalog->NumEntries)
      {
        shader_catalog_entry *Entry = Catalog->Entry + I;
        if (Entry->WatcherHandle == Iter.WatcherHandle) {
          if (glIsProgram(Entry->Program)) {
            glDeleteProgram(Entry->Program);
          }
          
          scoped_arena ScopedArena(Catalog->TransientArena);
          Entry->Program = GLCompileAndLinkShaders(&ScopedArena, Platform, (char*)File.Data);
          Platform->Interface.FreeEntireFile(&File);
          
          if (Entry->Program == 0) {
            Platform->Interface.Log("error: failed to reload shader: '%s'\n", Iter.FileName);
            Result = false;
          } else {
            Platform->Interface.Log("info: successfully reloaded shader: '%s'\n", Iter.FileName);
            Result = true;
          }
          
          break;
        }
      }
    } else {
      Platform->Interface.Log("error: failed to reload file '%s'\n", Iter.FileName);
    }
    
    Iter = WatchedFileIterNext(&Catalog->Watcher, Iter);
  }
  
  return(Result);
}

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
    Platform->Interface.Log("error: failed to load shader:'%s'\n",
                            Shader->Internal.ShaderFileName);
  }
  else
  {
    Platform->Interface.Log("info: successfully loaded shader:'%s'\n",
                            Shader->Internal.ShaderFileName);
  }
}

internal void ShaderLoad(shader *Shader, platform_state *Platform, game_state *GameState, char *ShaderFile)
{
  scoped_arena ScopedArena(&GameState->TransientArena);
  
  strncpy(Shader->Internal.ShaderFileName, ShaderFile, 128);
  
  Shader->Internal.ShaderFile = WatchedFile(Shader->Internal.ShaderFileName);
  if (WatchedFileHasError(&Shader->Internal.ShaderFile))
  {
    Platform->Interface.Log("error: unable to watch file '%s': %s\n",
                            ShaderFile,
                            WatchedFileGetError(&Shader->Internal.ShaderFile));
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
    Platform->Interface.Log("error: unable to watch file %s: %s\n",
                            ShaderFileName,
                            WatchedFileGetError(&Shader->Internal.ShaderFile));
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
