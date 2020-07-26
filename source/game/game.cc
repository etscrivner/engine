#include <stdio.h>

#include "game.h"
#include "common/language_layer.h"
#include "common/memory_arena.h"

// Unity build includes
#include "shaders.cc"
#include "renderer.cc"
#include "fonts.cc"
#include "textures.cc"
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
static char *BitmapFontShaderFile = "../assets/shaders/bitmap_font.gl";
static char *LineShaderFile = "../assets/shaders/line.gl";
static char *FilledRectShaderFile = "../assets/shaders/filled_rect.gl";
static char *FilledCircleShaderFile = "../assets/shaders/filled_circle.gl";
static char *TexturedQuadShaderFile = "../assets/shaders/textured_quad.gl";

void UpdateAudio(game_state *GameState, platform_state *Platform, f32 DeltaTimeSecs)
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

void SimulateGame(game_state *GameState, platform_state *Platform, f32 DeltaTimeSecs)
{
  // Process input
  if (KeyPressed(Platform, KEY_esc))
  {
    Platform->Shared.IsRunning = false;
  }

  if (KeyPressed(Platform, KEY_f2))
  {
    Platform->Shared.FullScreen = !Platform->Shared.FullScreen;
  }
  
  if (!ConsoleIsActive(&GameState->Console))
  {
    if (KeyPressed(Platform, KEY_f1))
    {
      ConsoleLog(&GameState->Console, "info: Forcing async texture load.");
      GameState->TextureCatalog.AllowAsync = !GameState->TextureCatalog.AllowAsync;
    }
  }
  
  GameState->Accum += 1/60.0f;
  
  if (GameState->Accum > 0.1f) {
    GameState->Accum = 0.0f;
    GameState->TextureOffset = (GameState->TextureOffset + 32) % 128;
  }
  
  // Simulate
  GameState->VideoTime += 1 / 60.0f;
  if (ConsoleIsActive(&GameState->Console)) {
    ConsoleInputMouse(&GameState->Console, Platform->Input.Mouse.Pos);
  }
  
  // Render (Simple Test Render Pipeline)
  RendererBeginFrame(&GameState->Renderer, Platform, Platform->Input.RenderDim);
  {
    RendererSetTarget(&GameState->Renderer, &GameState->HDRTarget);
    RendererClear(&GameState->Renderer, V4(0, 0, 0, 0));
    
    // Render Toy Shader
    GLuint Shader;
    Shader = ShaderCatalogUse(&GameState->ShaderCatalog, "toy_shader");
    {
      glBindVertexArray(GameState->AllPurposeVAO);
      {
        glUniform2f(glGetUniformLocation(Shader, "Mouse"), GameState->MousePos.X, GameState->MousePos.Y);
        glUniform2f(glGetUniformLocation(Shader, "RenderDim"), GameState->RenderDim.Width, GameState->RenderDim.Height);
        glUniform1f(glGetUniformLocation(Shader, "Time"), GameState->VideoTime);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
    }
    glUseProgram(0);
    
    Renderer2DRightHanded(&GameState->Renderer, GameState->RenderDim);
    RendererPushLine(&GameState->Renderer, 0, V2(0, 0), V2(1920, 1080), V4(1, 1, 1, 1));
    RendererPushFilledRect(&GameState->Renderer, 0, V4(100, 200, 200, 400), V4(1, 0, 0, 1));
    RendererPushFilledRect(&GameState->Renderer, 0, V4(200, 200, 200, 300), V4(0, 1, 0, 1));
    RendererPushFilledCircle(&GameState->Renderer, 0, V2(GameState->RenderDim.Width / 2, GameState->RenderDim.Height / 2), 200, V4(1, 1, 0, 1));
    RendererPushFilledRect(&GameState->Renderer, 0, V4(GameState->RenderDim.Width / 2 - 25, GameState->RenderDim.Height / 2 - 25, 25, 25), V4(1, 0, 0, 1));
    texture Monk = TextureCatalogGet(&GameState->TextureCatalog, Platform, "monk_idle");
    RendererPushTexture(&GameState->Renderer,
                        0,
                        Monk,
                        V4(GameState->TextureOffset, 0, 32, 32),
                        V4(100, 300, 256, 256),
                        V4(1, 1, 1, 1));
    RendererPushTexture(&GameState->Renderer,
                        0,
                        Monk,
                        V4(GameState->TextureOffset, 0, 32, 32),
                        V4(800, 800, 256, 256),
                        V4(1, 1, 1, 1));
    
    texture Guy = TextureCatalogGet(&GameState->TextureCatalog, Platform, "guy_idle");
    RendererPushTexture(&GameState->Renderer,
                        0,
                        Guy,
                        V4(GameState->TextureOffset, 0, 32, 32),
                        V4(300, 300, 256, 256),
                        V4(1, 1, 1, 1));
    RendererPopMVPMatrix(&GameState->Renderer);
    RendererFlush(&GameState->Renderer);
    
    // NOTE: UI pass goes here
    Renderer2DRightHanded(&GameState->Renderer, GameState->RenderDim);
    static char MousePosText[256];
    ConsoleUpdate(&GameState->Console, GameState, Platform, DeltaTimeSecs);

    
    snprintf(MousePosText, 256, "(%0.0f, %0.0f)", Platform->Input.Mouse.Pos01.X * GameState->RenderDim.Width, Platform->Input.Mouse.Pos01.Y * GameState->RenderDim.Height);
    RendererPushText(&GameState->Renderer, 0, &GameState->TitleFont, MousePosText, V2(GameState->RenderDim.Width / 2, GameState->RenderDim.Height / 2), V4(0, 0, 0, 1));

    snprintf(MousePosText, 256, "FPS: %0.00f, MCPF: %d", GameState->FPS, GameState->MCPF);
    RendererPushText(&GameState->Renderer, 0, &GameState->TitleFont, MousePosText, V2(0, FontTextHeightPixels(&GameState->TitleFont)), V4(1, 1, 1, 1));
    RendererPopMVPMatrix(&GameState->Renderer);
    RendererFlush(&GameState->Renderer);
    
    // FXAA Pass
#ifdef FXAA_PASS
    RendererClearTarget(&GameState->Renderer);
    RendererSetTarget(&GameState->Renderer, &GameState->FXAATarget);
    RendererClear(&GameState->Renderer, V4(0, 0, 0, 0));
    Shader = ShaderCatalogUse(&GameState->ShaderCatalog, "fxaa");
    {
      glBindVertexArray(GameState->AllPurposeVAO);
      {
        FramebufferBindToTexture(&GameState->HDRTarget, GL_TEXTURE0);
        glUniform2f(glGetUniformLocation(Shader, "TexResolution"), GameState->RenderDim.Width, GameState->RenderDim.Height);
        glUniform1i(glGetUniformLocation(Shader, "Texture"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
    }
    glUseProgram(0);
#endif

    // Gamma Correction and HDR => LDR Tone Mapping
    RendererClearTarget(&GameState->Renderer);
    //RendererClear(&GameState->Renderer, V4(0, 0, 0, 0));
    Shader = ShaderCatalogUse(&GameState->ShaderCatalog, "tone_mapper");
    {
      glBindVertexArray(GameState->AllPurposeVAO);
      {
#ifdef FXAA_PASS
        FramebufferBindToTexture(&GameState->FXAATarget, GL_TEXTURE0);
#else
        FramebufferBindToTexture(&GameState->HDRTarget, GL_TEXTURE0);
#endif // FXAA_PASS
        glUniform1i(glGetUniformLocation(Shader, "HDRBuffer"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
      glBindVertexArray(0);
    }
    glUseProgram(0);
    
    // TODO: Render everything into a final multi-sampled framebuffer then
    // blit this to the screen framebuffer for MSAA rendering.
  }
  RendererEndFrame(&GameState->Renderer);
}

void Update(platform_state *Platform, f32 DeltaTimeSecs)
{
  game_state *GameState = FetchGameState(Platform);
  
  switch (GameState->Mode) {
    case PROGRAM_MODE_game:
    {
      SimulateGame(GameState, Platform, DeltaTimeSecs);
    }
    break;
    default: break;
  }

  UpdateAudio(GameState, Platform, DeltaTimeSecs);
  
  // Hot reload catalogs if needed
  ShaderCatalogUpdate(&GameState->ShaderCatalog, Platform);
  TextureCatalogUpdate(&GameState->TextureCatalog, Platform);
}

void Shutdown(platform_state *Platform)
{
  game_state *GameState = FetchGameState(Platform);
  RendererDestroy(&GameState->Renderer);
  ShaderCatalogDestroy(&GameState->ShaderCatalog);
  TextureCatalogDestroy(&GameState->TextureCatalog);
  glDeleteBuffers(1, &GameState->AllPurposeVAO);
  
  FramebufferDestroy(&GameState->HDRTarget);
  FramebufferDestroy(&GameState->FXAATarget);
  
  FontManagerDestroyFont(&GameState->FontManager, &GameState->TitleFont);
  FontManagerDestroy(&GameState->FontManager);
}

static struct timespec StartTime = {};
static u64 StartCycles = 0;

void OnFrameStart(platform_state *Platform)
{
  game_state *GameState = FetchGameState(Platform);
  // TODO: Create a platform-layer performance counter to replace this. The
  // current GetTimeSecs is not accurate enough.
  clock_gettime(CLOCK_MONOTONIC_RAW, &StartTime);
  StartCycles = __rdtsc();
  //GameState->FrameStartTime = Platform->Interface.GetTimeMs();
  //printf("%0.02f\n", GameState->FrameStartTime);
}

void OnFrameEnd(platform_state *Platform)
{
  game_state *GameState = FetchGameState(Platform);
  struct timespec EndTime;
  clock_gettime(CLOCK_MONOTONIC_RAW, &EndTime);
  u64 Ticks = ((EndTime.tv_sec - StartTime.tv_sec) * 1e9) + (EndTime.tv_nsec - StartTime.tv_nsec);
  u64 EndCycles = __rdtsc();
  GameState->FPS = 1e9 / (f32)Ticks;
  GameState->MCPF = SafeTruncateUInt64((EndCycles - StartCycles) / (1000 * 1000));
  //GameState->FPS = 1.0f / ((Platform->Interface.GetTimeMs() - GameState->FrameStartTime) / 1000.0f);
}

///////////////////////////////////////////////////////////////////////////////

internal game_state* FetchGameState(platform_state *Platform)
{
  game_state *GameState = (game_state*)Platform->Input.PermanentStorage;
  if (!GameState->IsInitialized)
  {
    Platform->Shared.VSync = false;
    
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
      GameState->RenderDim = V2(1920, 1080);
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
    
    GameState->Mode = PROGRAM_MODE_game;
    GameState->AudioTime = 0.0f;
    GameState->VideoTime = 0.0f;
    
    // NOTE(eric): Clear to all zeros to avoid alpha-blending issues when
    // reconciling HDR => LDR.
    GameState->ClearColor = V4(0, 0, 0, 0);
    
#if 1
    const char* FontFace = "PragmataPro_Regular.ttf";
    //const char* FontFace = "Hack-Regular.ttf";
#else
    //const char* FontFace = "../assets/fonts/PragmataPro_Bold.ttf";
    const char *FontFace = "CenturySchoolbookRegular.pfb";
#endif
    
    FontManagerInit(&GameState->FontManager, "../assets/fonts");
    FontManagerLoadFont(&GameState->FontManager, &GameState->TitleFont, FontFace, 24); 
   
    glGenVertexArrays(1, &GameState->FontVAO);
    glGenBuffers(1, &GameState->FontVBO);
    glBindVertexArray(GameState->FontVAO);
    glBindBuffer(GL_ARRAY_BUFFER, GameState->FontVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(f32) * 4 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    {
      // NOTE: The renderer depends on the presence of certain shaders in the 
      // shader catalog in order to render primitives. These shaders should be
      // loaded here and eventually replaced by constants from a packfile or 
      // similar on production release.
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, LineShaderFile, "line");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, FilledRectShaderFile, "filled_rect");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, FilledCircleShaderFile, "filled_circle");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, TexturedQuadShaderFile, "textured_quad");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, BitmapFontShaderFile, "bitmap_font");
      
      // NOTE: Toy shaders that may be moved into the renderer later
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, ToyShaderFile, "toy_shader");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, ToneMapperFile, "tone_mapper");
      ShaderCatalogAdd(&GameState->ShaderCatalog, Platform, FXAAShaderFile, "fxaa");
    }
    
    // Initialize UI components
    {
      ConsoleCreate(&GameState->Console, &GameState->TitleFont);

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
    
    GameState->IsInitialized = true;
  }

  // Every frame, do these things when game state is fetched
  {
    // NOTE(eric): Use the platform normalized mouse position to calculate the
    // mouse position in the viewport. Since we render at a constant 
    // resolution regardless of window size this is generally not the same as
    // the "window mouse position".
    GameState->MousePos = GameState->RenderDim * Platform->Input.Mouse.Pos01;
    
    // NOTE(eric): Convert the calculate viewport space position to clip space
    // so that we can use it to unproject mouse coordinates in our tools.
    GameState->MouseClip = ScreenToClipSpace(GameState->MousePos, GameState->RenderDim);
  }
  
  return(GameState);
}
