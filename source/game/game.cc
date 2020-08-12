#include <stdio.h>

#include "game.h"
#include "common/language_layer.h"
#include "common/memory_arena.h"

// Unity build includes
#include "shaders.cc"
#include "renderer.cc"
#include "fonts.cc"
#include "textures.cc"
#include "sounds.cc"
#include "mixer.cc"
#include "ui/ui.cc"
#include "ui/debug_console.cc"

// Internal interfaces defined below platform layer
internal game_state* FetchGameState(platform_state *Platform);

typedef struct render_resolution {
  u32 Width;
  u32 Height;
  const char *Name;
} render_resolution;

// NOTE: Instead of allowing for arbitrary window resizing. We have opted to
// render in a handful of standard resolutions and disallow resizing.
internal render_resolution AvailableRenderResolutions[] = {
  [0] = { 1280,  720,  "720p" },
  [1] = { 1920, 1080, "1080p" }
};

//#define FXAA_PASS

// NOTE(eric): Shader file paths need to go into permanent storage in order
// to avoid issues with pointers when the library is hot reloaded.
static char *ToyShaderFile = "../assets/shaders/toy_shader.gl";
static char *ToneMapperFile = "../assets/shaders/tone_mapper.gl";
static char *FXAAShaderFile = "../assets/shaders/fxaa.gl";
static char *PackedBitmapFontShaderFile = "../assets/shaders/bitmap_font_packed.gl";
static char *LineShaderFile = "../assets/shaders/line.gl";
static char *FilledRectShaderFile = "../assets/shaders/filled_rect.gl";
static char *FilledCircleShaderFile = "../assets/shaders/filled_circle.gl";
static char *TexturedQuadShaderFile = "../assets/shaders/textured_quad.gl";

#if 0
// NOTE: This method is left over from testing the platform layer audio. It is
// a good, continuous sound test for new platform audio layers that can help
// detect pops and other audio issues.
void TestUpdateAudio(game_state *GameState, platform_state *Platform, f32 DeltaTimeSecs)
{
  i16 ToneVolume = 3000;
  i32 ToneHz = (250 + (Platform->Input.Mouse.Pos01.X - 0.25) * 150);
  i32 WavePeriod = Platform->Shared.AudioBuffer.SamplesPerSecond / ToneHz;
  i16* SampleOut = Platform->Shared.AudioBuffer.Samples;
  for (u32 I = 0; I < Platform->Shared.AudioBuffer.FrameCount; ++I)
  {
    f32 SineValue = sinf(GameState->AudioTime);
    i16 Value = (i32)(SineValue * ToneVolume);
    if (Value < -32768)
      Value = -32768;
    if (Value > 32767)
      Value = 32767;
    *SampleOut++ = Value;
    *SampleOut++ = Value;
    GameState->AudioTime += TAU * (1.0f / (f32)WavePeriod);
    if (GameState->AudioTime > TAU)
    {
      GameState->AudioTime -= TAU;
    }
  }
}
#endif

// NOTE: General structure of a subsystem of the engine
#if 0
void SubsystemUpdate(game_state *GameState, platform_state *Platform, f32 DeltaTimeSecs)
{
  // Update console

  // Define UI

  // Update components
  if (!GameState->KeyboardInputConsumed)
  {
    // Process keyboard input
  }

  if (!GameState->MouseInputConsumed)
  {
    // Process mouse input
  }

  if (!GameState->TextInputConsumed)
  {
    // Process text input
  }

  {
    // Subsystem updates
  }

  // Render subystem

  // Render UI

  // Render Console
}
#endif

void SimulateGame(app_context Ctx, u64 DeltaTimeMicros)
{
  renderer *Renderer = &Ctx.Game->Renderer;
  ui_context *UI = &Ctx.Game->UI;
  audio_player *AudioPlayer = &Ctx.Game->AudioPlayer;

  // Update console
  ConsoleUpdate(&Ctx.Game->Console, Ctx, DeltaTimeMicros);
  Ctx.Game->KeyboardInputConsumed |= Ctx.Game->Console.KeyboardInputConsumed;
  Ctx.Game->MouseInputConsumed |= Ctx.Game->Console.MouseInputConsumed;
  Ctx.Game->TextInputConsumed |= Ctx.Game->Console.TextInputConsumed;

  // UI Definition: Will not be rendered until command list is processed
  // 
  // UI components should be rendered first so that information about
  // whether mouse or keyboard input is captured can be sent to the rest of
  // the application.
  if (!Ctx.Game->KeyboardInputConsumed)
  {
    // TODO: Update UI with new keyboard input
  }

  if (!Ctx.Game->MouseInputConsumed)
  {
    // TODO: Update UI with new mouse input
  }

  if (!Ctx.Game->TextInputConsumed)
  {
    // TODO: Update UI with new text input
  }

  // Process input
  if (KeyPressed(Ctx.Platform, KEY_esc))
  {
    Ctx.Platform->Shared.IsRunning = false;
  }

  if (KeyPressed(Ctx.Platform, KEY_f2))
  {
    Ctx.Platform->Shared.FullScreen = !Ctx.Platform->Shared.FullScreen;
  }
  
  if (KeyPressed(Ctx.Platform, KEY_f3))
  {
    AudioPlayerPlaySound(AudioPlayer, Ctx.Game->SlideSound, V2(1.0f), false);
  }

  // Render (Simple Test Render Pipeline)
  RendererBeginFrame(Renderer, Ctx.Platform, Ctx.Platform->Input.RenderDim);
  {
    RendererSetTarget(Renderer, &Ctx.Game->HDRTarget);
    RendererClear(Renderer, V4(0, 0, 0, 0));
    
    // Render Game Data
    Renderer2DRightHanded(Renderer, Ctx.Game->RenderDim);
    {
    }
    RendererPopMVPMatrix(Renderer);
    RendererFlush(Renderer);
    
    // Render UI and Overlays
    {
      // Render UI
      BeginWidgets(&Ctx.Game->UIState, Ctx);
      {
        if (WidgetWindowBegin(&Ctx.Game->UIState, Ctx, V4(90, 350, 300, 300), "Test Window", &Ctx.Game->Window[0]))
        {
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(100, 400, 200, 50), "Clickaroo"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO");
          }
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(150, 450, 200, 50), "Clickaroo 2"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO2");
          }
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(100, 450, 200, 50), "Clickaroo 3"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO3");
          }
          if (WidgetCheckbox(&Ctx.Game->UIState, Ctx, V4(100, 450, 200, 50), "VSync", &Ctx.Platform->Shared.VSync))
          {
            ConsoleLog(&Ctx.Game->Console, "CHECKED");
          }

          WidgetWindowEnd(&Ctx.Game->UIState);
        }

        if (WidgetWindowBegin(&Ctx.Game->UIState, Ctx, V4(500, 250, 300, 300), "Other Window", &Ctx.Game->Window[1]))
        {
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(500, 400, 200, 50), "Clickaroo"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO (Window 2)");
          }
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(550, 350, 200, 50), "Clickaroo 2"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO2 (Window 2)");
          }
          WidgetWindowEnd(&Ctx.Game->UIState);
        }

        if (WidgetWindowBegin(&Ctx.Game->UIState, Ctx, V4(700, 450, 300, 300), "Third Window", &Ctx.Game->Window[2]))
        {
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(500, 400, 200, 50), "Clickaroo"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO (Window 3)");
          }
          if (WidgetButton(&Ctx.Game->UIState, Ctx, V4(550, 350, 200, 50), "Clickaroo 2"))
          {
            ConsoleLog(&Ctx.Game->Console, "CLIACKAROO2 (Window 3)");
          }
          WidgetWindowEnd(&Ctx.Game->UIState);
        }
      }
      EndWidgets(&Ctx.Game->UIState);

      // Render the debug console
      ConsoleRender(&Ctx.Game->Console, Renderer);

      static char FPSText[256];
      snprintf(FPSText, 256, "FPS: %0.00f, MCPF: %03d, MSPF: %0.04f", Ctx.Game->FPS, Ctx.Game->MCPF, Ctx.Game->MSPF);

      f32 TextWidth = FontTextWidthPixels(&Ctx.Game->MonoFont, FPSText);

      if (DrawCheckbox(Renderer, Ctx, &Ctx.Game->TextureCatalog, "VSync", V4(TextWidth + 10, FontTextHeightPixels(&Ctx.Game->MonoFont) - 8, 32, 32), DefaultButtonStyle, &Ctx.Platform->Shared.VSync))
      {
        ConsoleLog(&Ctx.Game->Console, "CHECKBOX");
      }

      if (DrawButton(Renderer, Ctx, "Reopen Windows", V4(TextWidth + 115, FontTextHeightPixels(&Ctx.Game->MonoFont) - 10, 190, 40), DefaultButtonStyle))
      {
        ConsoleLog(&Ctx.Game->Console, "CLICKED");
        foreach (I, ArrayCount(Ctx.Game->Window)) {
          Ctx.Game->Window[I].IsOpen = true;
        }
      }

      // Render FPS output
      Renderer2DRightHanded(Renderer, Ctx.Game->RenderDim);
      {
        RendererPushText(
          Renderer, 0, &Ctx.Game->MonoFont, FPSText, V2(0, FontTextHeightPixels(&Ctx.Game->MonoFont)), V4(1, 1, 1, 1)
        );
      }
      RendererPopMVPMatrix(Renderer);
    }
    RendererFlush(Renderer);
    
    // FXAA Pass
    GLuint Shader;
#ifdef FXAA_PASS
    RendererClearTarget(Renderer);
    RendererSetTarget(Renderer, &Ctx.Game->FXAATarget);
    RendererClear(Renderer, V4(0, 0, 0, 0));
    Shader = ShaderCatalogUse(&Ctx.Game->ShaderCatalog, "fxaa");
    {
      glBindVertexArray(Ctx.Game->AllPurposeVAO);
      {
        FramebufferBindToTexture(&Ctx.Game->HDRTarget, GL_TEXTURE0);
        glUniform2f(glGetUniformLocation(Shader, "u_TexResolution"), Ctx.Game->RenderDim.Width, Ctx.Game->RenderDim.Height);
        glUniform1i(glGetUniformLocation(Shader, "u_Texture"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
    }
    glUseProgram(0);
#endif

    // Gamma Correction and HDR => LDR Tone Mapping
    RendererClearTarget(Renderer);
    //RendererClear(&GameState->Renderer, V4(0, 0, 0, 0));
    Shader = ShaderCatalogUse(&Ctx.Game->ShaderCatalog, "tone_mapper");
    {
      glBindVertexArray(Ctx.Game->AllPurposeVAO);
      {
#ifdef FXAA_PASS
        FramebufferBindToTexture(&Ctx.Game->FXAATarget, GL_TEXTURE0);
#else
        FramebufferBindToTexture(&Ctx.Game->HDRTarget, GL_TEXTURE0);
#endif // FXAA_PASS
        glUniform1i(glGetUniformLocation(Shader, "u_HDRBuffer"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
    }
    glUseProgram(0);
    
    // TODO: Render everything into a final multi-sampled framebuffer then
    // blit this to the screen framebuffer for MSAA rendering.
  }
  RendererEndFrame(Renderer);
}

void Update(platform_state *Platform, u64 DeltaTimeMicros)
{
  game_state *GameState = FetchGameState(Platform);
  app_context Ctx = {GameState, Platform};
  
  switch (GameState->Mode) {
  case PROGRAM_MODE_game:
  {
    SimulateGame(Ctx, DeltaTimeMicros);
  }
  break;
  default: break;
  }

  //UpdateAudio(GameState, Platform, DeltaTimeSecs);
  UpdateAndMixAudio(&GameState->AudioPlayer, &Platform->Shared.AudioBuffer, (f32)DeltaTimeMicros / 1000000.0f);
  
  // Hot reload catalogs if needed
  ShaderCatalogUpdate(&GameState->ShaderCatalog, Platform);
  TextureCatalogUpdate(&GameState->TextureCatalog, Platform);
}

void Shutdown(platform_state *Platform)
{
  game_state *GameState = FetchGameState(Platform);

  FramebufferDestroy(&GameState->HDRTarget);
  FramebufferDestroy(&GameState->FXAATarget);
  RendererDestroy(&GameState->Renderer);
  glDeleteBuffers(1, &GameState->AllPurposeVAO);
  
  TextureCatalogDestroy(&GameState->TextureCatalog);
  
  ShaderCatalogDestroy(&GameState->ShaderCatalog);
  
  FontManagerDestroyFont(&GameState->FontManager, &GameState->MonoFont);
  FontManagerDestroyFont(&GameState->FontManager, &GameState->UIFont);
  FontManagerDestroy(&GameState->FontManager);

  AudioPlayerDestroy(&GameState->AudioPlayer);
  SoundManagerDestroySound(&GameState->SoundManager, &GameState->SlideSound, Platform);
  SoundManagerDestroySound(&GameState->SoundManager, &GameState->WallMarketTheme, Platform);
  SoundManagerDestroy(&GameState->SoundManager);
}

void OnFrameStart(platform_state *Platform)
{
  game_state *GameState = FetchGameState(Platform);
  GameState->StartCycles = __rdtsc();
  GameState->FrameStartTime = Platform->Interface.GetTimeMs();
  GameState->FrameTimeSamples[GameState->FrameTimeSampleIndex] = Platform->Interface.GetTimeMs();
}

void OnFrameEnd(platform_state *Platform)
{
  game_state *GameState = FetchGameState(Platform);

  // Calculate cycles per frame
  u64 EndCycles = __rdtsc();
  GameState->MCPF = SafeTruncateUInt64((EndCycles - GameState->StartCycles) / (1000 * 1000));

  // Calculate average FPS
  GameState->FrameTimeSamples[GameState->FrameTimeSampleIndex] = Platform->Interface.GetTimeMs() - GameState->FrameTimeSamples[GameState->FrameTimeSampleIndex];
  ++GameState->FrameTimeSampleIndex;
  if (GameState->FrameTimeSampleIndex >= ArrayCount(GameState->FrameTimeSamples))
  {
    GameState->FrameTimeLooped = true;
    GameState->FrameTimeSampleIndex = 0;
  }
  u64 FPSTotal = 0;
  u32 MaxIndex = ArrayCount(GameState->FrameTimeSamples);
  if (!GameState->FrameTimeLooped)
  {
    MaxIndex = GameState->FrameTimeSampleIndex;
  }
  foreach (I, MaxIndex)
  {
    FPSTotal += GameState->FrameTimeSamples[I];
  }
  GameState->MSPF = (f32)FPSTotal / MaxIndex;
  GameState->FPS = 1.0f / ((f32)FPSTotal / (MaxIndex * 1000.0f));
}

///////////////////////////////////////////////////////////////////////////////

internal game_state* FetchGameState(platform_state *Platform)
{
  game_state *GameState = (game_state*)Platform->Input.PermanentStorage;

  // Initialize
  if (!GameState->IsInitialized)
  {
    {
      GameState->PermanentArena = ArenaInit(Platform->Input.PermanentStorage + sizeof(game_state),
                                            Platform->Input.PermanentStorageSize - sizeof(game_state));
      GameState->TransientArena = ArenaInit(Platform->Input.TransientStorage,
                                            Platform->Input.TransientStorageSize);
    }
    
    
    {
      ShaderCatalogInit(&GameState->ShaderCatalog, &GameState->TransientArena);
      TextureCatalogInit(&GameState->TextureCatalog);
      RendererCreate(Platform, &GameState->Renderer, &GameState->ShaderCatalog);
      
      // TODO: Replace with configurable rendering resolution
      GameState->RenderDim = V2U(1920, 1080);
      GameState->Mode = PROGRAM_MODE_game;
      GameState->AudioTime = 0.0f;
      GameState->PlayerP = V2(0, 0);
    }
    
    glGenVertexArrays(1, &GameState->AllPurposeVAO);

    GameState->HDRTarget = FramebufferCreate(GameState->RenderDim.Width, GameState->RenderDim.Height);
    FramebufferAttachTexture(&GameState->HDRTarget, FRAMEBUFFER_TEXTURE_FORMAT_hdr);
    if (!FramebufferIsValid(&GameState->HDRTarget))
    {
      Platform->Interface.Log("error: hdr framebuffer not complete.\n");
    }

    GameState->FXAATarget = FramebufferCreate(GameState->RenderDim.Width, GameState->RenderDim.Height);
    FramebufferAttachTexture(&GameState->FXAATarget, FRAMEBUFFER_TEXTURE_FORMAT_rgba);
    if (!FramebufferIsValid(&GameState->FXAATarget))
    {
      Platform->Interface.Log("error: fxaa framebuffer not complete.\n");
    }
    
    // Font manager
#if 1
    const char* FontFace = "PragmataPro_Bold.ttf";
    //const char* FontFace = "PragmataPro_Regular.ttf";
    //const char* FontFace = "Hack-Regular.ttf";
#else
    //const char* FontFace = "../assets/fonts/PragmataPro_Bold.ttf";
    const char *FontFace = "CenturySchoolbookRegular.pfb";
#endif
    
    FontManagerInit(&GameState->FontManager, "../assets/fonts");
    FontManagerLoadFont(&GameState->FontManager, &GameState->MonoFont, FontFace, 24, &GameState->TransientArena);
    FontManagerLoadFont(&GameState->FontManager, &GameState->UIFont, FontFace, 16, &GameState->TransientArena);

    // Sound manager
    SoundManagerInit(&GameState->SoundManager, "../assets/sounds");
    SoundManagerLoadSound(&GameState->SoundManager, &GameState->SlideSound, Platform, "boxslide.ogg");
    SoundManagerLoadSound(&GameState->SoundManager, &GameState->WallMarketTheme, Platform, "wall_market_theme.ogg");

    AudioPlayerInit(&GameState->AudioPlayer, &GameState->PermanentArena);
    //AudioPlayerPlaySound(&GameState->AudioPlayer, GameState->WallMarketTheme, V2(1.0f), false);
   
    {
      // NOTE: The renderer depends on the presence of certain shaders in the 
      // shader catalog in order to render primitives. These shaders should be
      // loaded here and eventually replaced by constants from a packfile or 
      // similar on production release.
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, LineShaderFile, "line");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, FilledRectShaderFile, "filled_rect");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, FilledCircleShaderFile, "filled_circle");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, TexturedQuadShaderFile, "textured_quad");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, PackedBitmapFontShaderFile, "bitmap_font");
      
      // NOTE: Toy shaders that may be moved into the renderer later
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, ToneMapperFile, "tone_mapper");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, FXAAShaderFile, "fxaa");
    }
    
    // Initialize UI components
    {
      ConsoleCreate(&GameState->Console, &GameState->MonoFont, &GameState->TransientArena);

      // Insert some test data into the debug console to overflow the content
      // area and trigger the scroll bar.
#if 1
      const char* LipsumLine[] = {
        "Lorem ipsum dolor sit amet,",
        "consectetur adipiscing elit.",
        "Praesent velit tortor, ",
        "sodales non tortor id, ",
        "fermentum malesuada diam.",
        "Morbi volutpat pulvinar est a semper",
        "Quisque rhoncus arcu a ligula dignissim",
        "scelerisque aliquam ligula vehicula",
        "Sed turpis tortor,",
        "finibus nec velit ut,",
        "volutpat suscipit libero.",
        "Sed ut ligula sit amet lorem mollis imperdiet id non lectus.",
        "Quisque ac quam quis ex accumsan ultrices.",
        "Mauris nec placerat libero, sed consectetur sem.",
        "Sed malesuada egestas consectetur.",
        "Morbi commodo consequat rhoncus.",
        "Vestibulum ut dolor augue.",
        "Nunc eu eleifend elit, eget efficitur felis.",
        "In at justo id mi porta condimentum."
      };
      foreach(I, ArrayCount(LipsumLine)) {
        ConsoleLog(&GameState->Console, LipsumLine[I]);
      }
#endif
    }

    GameState->UIState = {};
    UIStateInit(&GameState->UIState, &GameState->UIFont, &GameState->TextureCatalog, &GameState->Renderer);
    foreach(I, ArrayCount(GameState->Window)) {
      GameState->Window[I].IsOpen = true;
    }

    GameState->IsInitialized = true;
  }

  // Update (per-frame)
  {
    // NOTE(eric): Use the platform normalized mouse position to calculate the
    // mouse position in the viewport. Since we render at a constant 
    // resolution regardless of window size this is generally not the same as
    // the "window mouse position".
    GameState->MousePos = V2I(
      GameState->RenderDim.X * Platform->Input.Mouse.Pos01.X,
      GameState->RenderDim.Y * Platform->Input.Mouse.Pos01.Y
    );
    
    // NOTE(eric): Convert the calculated viewport space position to clip space
    // so that we can use it to unproject mouse coordinates in our tools.
    //GameState->MouseClip = ScreenToClipSpace(GameState->MousePos, GameState->RenderDim);

    // Clear these to false ever frame to allow more subsystems to consume
    // input
    GameState->KeyboardInputConsumed = false;
    GameState->MouseInputConsumed = false;
    GameState->TextInputConsumed = false;
    DefaultButtonStyle.Font = &GameState->MonoFont;
  }
  
  return(GameState);
}
