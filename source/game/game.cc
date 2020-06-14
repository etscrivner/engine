#include <stdio.h>

#include "game.h"
#include "common/language_layer.h"
#include "common/memory_arena.h"

// Unity build includes
#include "shaders.cc"
#include "renderer.cc"

// Internal interfaces defined below platform layer
internal game_state* FetchGameState(platform_state *Platform);

extern "C" {
  // NOTE(eric): Shader file paths need to go into permanent storage in order
  // to avoid issues with pointers when the library is hot reloaded.
  const char *ShaderFile = "../assets/shaders/quad_filled.gl";
  const char *ToneMapperFile = "../assets/shaders/tone_mapper.gl";
  const char *FXAAShaderFile = "../assets/shaders/fxaa.gl";

  void Update(platform_state *Platform, f32 DeltaTimeSecs)
  {
    game_state *GameState = FetchGameState(Platform);
    
    // Process input
    if (KeyPressed(Platform, KEY_esc))
    {
      Platform->Shared.IsRunning = false;
    }

    if (MousePressed(Platform, MOUSE_BUTTON_left))
    {
      Platform->Shared.FullScreen = !Platform->Shared.FullScreen;
    }

    if (MousePressed(Platform, MOUSE_BUTTON_middle))
    {
      scoped_arena ScopedArena(&GameState->TransientArena);
      char *Text = Platform->Interface.GetClipboardText(&ScopedArena);
      Platform->Interface.Log("Clipboard: '%s'\n", Text);
    }

    // Simulate
    GameState->VideoTime += 1 / 60.0f;

    // Render (Simple Test Render Pipeline)
    RendererBeginFrame(&GameState->Renderer, Platform, Platform->Input.RenderDim, GameState->ClearColor);
    {
      RendererSetTarget(&GameState->Renderer, &GameState->HDRTarget);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glClearColor(0, 0, 0, 0);
      // Render Toy Shader
      ShaderUse(&GameState->QuadShader);
      glBindVertexArray(GameState->AllPurposeVAO);
      {
        glUniform2f(glGetUniformLocation(GameState->QuadShader.Program, "Mouse"), Platform->Input.Mouse.Pos.X, Platform->Input.Mouse.Pos.Y);
        glUniform2f(glGetUniformLocation(GameState->QuadShader.Program, "RenderDim"), Platform->Input.RenderDim.Width, Platform->Input.RenderDim.Height);
        glUniform1f(glGetUniformLocation(GameState->QuadShader.Program, "Time"), GameState->VideoTime);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
      glUseProgram(0);

      // FXAA Pass
      //RendererClearTarget(&GameState->Renderer);
      RendererSetTarget(&GameState->Renderer, &GameState->FXAATarget);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glClearColor(0, 0, 0, 0);
      ShaderUse(&GameState->FXAAShader);
      glBindVertexArray(GameState->AllPurposeVAO);
      {
        FramebufferBindToTexture(&GameState->HDRTarget, GL_TEXTURE0);
        glUniform2f(glGetUniformLocation(GameState->FXAAShader.Program, "TexResolution"), Platform->Input.RenderDim.Width, Platform->Input.RenderDim.Height);
        glUniform1i(glGetUniformLocation(GameState->FXAAShader.Program, "Texture"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
      glUseProgram(0);

      // Gamma Correction and HDR => LDR Tone Mapping
      RendererClearTarget(&GameState->Renderer);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glClearColor(0, 0, 0, 0);
      //RendererSetTarget(&GameState->Renderer, &GameState->FXAATarget);
      ShaderUse(&GameState->ToneMapper);
      glBindVertexArray(GameState->AllPurposeVAO);
      {
        FramebufferBindToTexture(&GameState->FXAATarget, GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(GameState->ToneMapper.Program, "HDRBuffer"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
      glUseProgram(0);
    }
    RendererEndFrame(&GameState->Renderer);

    // Mix audio
    for (u32 I = 0; I < Platform->Shared.AudioBuffer.SizeSamples; I++) {
      i16 Value = (i16)(1000.0f * sinf(2*PI*GameState->AudioTime));
      Platform->Shared.AudioBuffer.Buffer[4*I] = Value;
      Platform->Shared.AudioBuffer.Buffer[4*I+1] = Value>>8;
      Platform->Shared.AudioBuffer.Buffer[4*I+2] = Value;
      Platform->Shared.AudioBuffer.Buffer[4*I+3] = Value>>8;
      GameState->AudioTime += (500.0*Platform->Input.Mouse.Pos01.X + 140)/44100;
      if (GameState->AudioTime > 1.0f) {
        GameState->AudioTime -= 1.0f;
      }
    }

    // Reload shaders
    ShaderHotLoad(Platform, &GameState->TransientArena, &GameState->QuadShader);
    ShaderHotLoad(Platform, &GameState->TransientArena, &GameState->ToneMapper);
    ShaderHotLoad(Platform, &GameState->TransientArena, &GameState->FXAAShader);
  }
  
  void Shutdown(platform_state *Platform)
  {
    game_state *GameState = FetchGameState(Platform);
    RendererDestroy(&GameState->Renderer);
    ShaderDestroy(&GameState->QuadShader);
    glDeleteBuffers(1, &GameState->AllPurposeVAO);

    FramebufferDestroy(&GameState->HDRTarget);
    FramebufferDestroy(&GameState->FXAATarget);
  }
  
  void OnFrameStart(platform_state *Platform)
  {}
  
  void OnFrameEnd(platform_state *Platform)
  {}
}

///////////////////////////////////////////////////////////////////////////////

internal game_state* FetchGameState(platform_state *Platform)
{
  game_state *GameState = (game_state*)Platform->Input.PermanentStorage;
  if (!GameState->IsInitialized)
  {
    Platform->Shared.VSync = true;

    {
      GameState->PermanentArena = ArenaInit(
        Platform->Input.PermanentStorage + sizeof(game_state),
        Platform->Input.PermanentStorageSize - sizeof(game_state)
      );
      GameState->TransientArena = ArenaInit(
        Platform->Input.TransientStorage,
        Platform->Input.TransientStorageSize
      );
    }

    {
      RendererCreate(&GameState->TransientArena, Platform, &GameState->Renderer);
    }

    ShaderLoad(&GameState->QuadShader, Platform, GameState, (char*)ShaderFile);
    ShaderLoad(&GameState->ToneMapper, Platform, GameState, (char*)ToneMapperFile);
    ShaderLoad(&GameState->FXAAShader, Platform, GameState, (char*)FXAAShaderFile);
    glGenVertexArrays(1, &GameState->AllPurposeVAO);

    GameState->HDRTarget = FramebufferCreate(Platform->Input.RenderDim.Width, Platform->Input.RenderDim.Height);
    FramebufferAttachTexture(&GameState->HDRTarget, FRAMEBUFFER_TEXTURE_FORMAT_hdr);
    if (!FramebufferIsValid(&GameState->HDRTarget))
    {
      Platform->Interface.Log("error: hdr framebuffer not complete.\n");
    }

    GameState->FXAATarget = FramebufferCreate(Platform->Input.RenderDim.Width, Platform->Input.RenderDim.Height);
    FramebufferAttachTexture(&GameState->FXAATarget, FRAMEBUFFER_TEXTURE_FORMAT_rgba);
    if (!FramebufferIsValid(&GameState->FXAATarget))
    {
      Platform->Interface.Log("error: fxaa framebuffer not complete.\n");
    }

    GameState->AudioTime = 0.0f;
    GameState->VideoTime = 0.0f;

    // NOTE(eric): Clear to all zeros to avoid alpha-blending issues when
    // reconciling HDR => LDR.
    GameState->ClearColor = V4(0, 0, 0, 0);
    
    GameState->IsInitialized = true;
  }

  return(GameState);
}
