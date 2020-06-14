#ifndef GAME_H
#define GAME_H

#include "common/language_layer.h"
#include "common/memory_arena.h"
#include "common/watched_file.h"

#ifdef PLATFORM_MACOS
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "ext/stb_image.h"

// Game configuration provided to platform
#define APP_TITLE              "Plague 2.0"
#define DEFAULT_TARGET_FPS     60.0f
#define PERMANENT_STORAGE_SIZE Megabytes(256)
#define TRANSIENT_STORAGE_SIZE Megabytes(256)

typedef struct button {
  // Pressed is only true if the button was just pressed this frame.
  b32 Pressed;
  // Down is true if the button was pressed in a previous frame and has not yet
  // been released.
  b32 Down;
} button;

///////////////////////////////////////////////////////////////////////////////
// mouse input

#define MouseDown(Platform, MouseButton) ((Platform)->Input.Mouse.Button[(MouseButton)].Down)
#define MousePressed(Platform, MouseButton) ((Platform)->Input.Mouse.Button[(MouseButton)].Pressed)

typedef enum mouse_button {
  MOUSE_BUTTON_left,
  MOUSE_BUTTON_middle,
  MOUSE_BUTTON_right,
  MOUSE_BUTTON_MAX
} mouse_button;

typedef struct mouse {
  v2 Pos;
  v2 Pos01; // Normalized position in range [0, 1]
  button Button[MOUSE_BUTTON_MAX];
} mouse;

///////////////////////////////////////////////////////////////////////////////
// keyboard

#define KeyDown(Platform, KeyboardKey) ((Platform)->Input.Keyboard.Key[(KeyboardKey)].Down)
#define KeyPressed(Platform, KeyboardKey) ((Platform)->Input.Keyboard.Key[(KeyboardKey)].Pressed)

enum {
#define Key(name, str) KEY_##name,
#include "keyboard_key_list.h"
  KEY_MAX
};

typedef struct keyboard {
  button Key[KEY_MAX];
} keyboard;

///////////////////////////////////////////////////////////////////////////////
// audio

typedef struct audio_buffer {
  u8  *Buffer;
  u64 Size;
  u32 SizeSamples;
} audio_buffer;

///////////////////////////////////////////////////////////////////////////////
// file system

typedef struct platform_entire_file {
  u8  *Data;
  u32 SizeBytes;
} platform_entire_file;

///////////////////////////////////////////////////////////////////////////////
// platform provided state and functions

// Platform provided functions
typedef f32 get_time_secs_fn(void);
typedef void* get_opengl_proc_address_fn(const char*);
typedef b32 load_entire_file_fn(const char*, platform_entire_file*);
typedef void free_entire_file_fn(platform_entire_file*);
typedef void log_fn(const char*, ...);
typedef b32 set_clipboard_text_fn(const char*);
typedef char* get_clipboard_text_fn(scoped_arena*);

typedef struct platform_state {
  struct {
    // NOTE(eric): These are expected to be initialized to all zeros
    u8  *PermanentStorage;
    u32 PermanentStorageSize;
    u8  *TransientStorage;
    u32 TransientStorageSize;
    
    v2 RenderDim;
    mouse Mouse;
    keyboard Keyboard;
  } Input;
  
  struct {
    b32 IsRunning;
    i32 TargetFPS;
    b32 VSync;
    b32 FullScreen;

    audio_buffer AudioBuffer;
  } Shared;
  
  struct {
    get_time_secs_fn *GetTimeSecs;
    get_opengl_proc_address_fn *GetOpenGLProcAddress;
    load_entire_file_fn *LoadEntireFile;
    free_entire_file_fn *FreeEntireFile;
    log_fn *Log;
    set_clipboard_text_fn *SetClipboardText;
    get_clipboard_text_fn *GetClipboardText;
  } Interface;
} platform_state;

///////////////////////////////////////////////////////////////////////////////
// game provided state and functions

#include "renderer.h" // renderer struct

// Normal updates
typedef void update_fn(platform_state*, f32);
typedef void shutdown_fn(platform_state*);

// Debug hooks from platform layer
typedef void on_frame_start_fn(platform_state*);
typedef void on_frame_end_fn(platform_state*);

extern "C" {
  void Update(platform_state *Platform, f32 DeltaTimeSecs);
  void Shutdown(platform_state *Platform);
  void OnFrameStart(platform_state *Platform);
  void OnFrameEnd(platform_state *Platform);
}

// game_state defines the state that the game itself stores
typedef struct game_state {
  b32 IsInitialized;

  memory_arena PermanentArena;
  memory_arena TransientArena;
  renderer Renderer;

  // TODO(eric): Move below eventually
  v4 ClearColor;
  f32 AudioTime;
  f32 VideoTime;
  
  shader QuadShader;
  GLuint AllPurposeVAO;

  shader ToneMapper;
  framebuffer HDRTarget;
  shader FXAAShader;
  framebuffer FXAATarget;
} game_state;

#endif // GAME_H
