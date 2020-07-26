#ifndef GAME_H
#define GAME_H

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

// Cross-platform threading
#define THREAD_IMPLEMENTATION
#include "ext/thread.h"

#include <ft2build.h>
#include FT_FREETYPE_H

// Include these down here as language_layer defines `internal` which is the
// name of several variables in other libraries.
#include "common/language_layer.h"
#include "common/memory_arena.h"
#include "common/watched_file.h"
#define WATCHED_FILE_SET_IMPLEMENTATION
#include "common/watched_file_set.h"
#include "fonts.h"

// Game configuration provided to platform
#define APP_TITLE              "Plague 2.0"
#define DEFAULT_TARGET_FPS     60.0f
#define PERMANENT_STORAGE_SIZE Megabytes(256)
#define TRANSIENT_STORAGE_SIZE Megabytes(256)
#define DEFAULT_WINDOW_WIDTH   1280 // 1920
#define DEFAULT_WINDOW_HEIGHT  720 // 1080

typedef struct button {
  // Pressed is only true if the button was just pressed this frame.
  b32 Pressed;
  // Down is true if the button was pressed in this frame or a previous frame
  // and has not yet been released.
  b32 Down;
  // Indicates whether or not this event is a repeat key press
  b32 IsRepeat;
} button;

///////////////////////////////////////////////////////////////////////////////
// mouse

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
  v2 Wheel; // Mouse wheel motion in X, Y direction
  button Button[MOUSE_BUTTON_MAX];
} mouse;

///////////////////////////////////////////////////////////////////////////////
// keyboard

#define KeyDown(Platform, KeyboardKey) ((Platform)->Input.Keyboard.Key[(KeyboardKey)].Down)
#define KeyPressed(Platform, KeyboardKey) ((Platform)->Input.Keyboard.Key[(KeyboardKey)].Pressed)
#define KeyPressedOrRepeat(Platform, KeyboardKey) (KeyPressed(Platform, KeyboardKey) || (Platform)->Input.Keyboard.Key[(KeyboardKey)].IsRepeat)

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
  // NOTE: Samples MUST be padded to a multiple of 4 samples!
  i16 *Samples;
  u32 FrameCount;
  u32 SamplesPerSecond;
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
typedef u64 get_time_ms_fn(void);
typedef void* get_opengl_proc_address_fn(const char*);
typedef b32 load_entire_file_fn(const char*, platform_entire_file*);
typedef void free_entire_file_fn(platform_entire_file*);
typedef void log_fn(const char*, ...);
typedef b32 set_clipboard_text_fn(const char*);
typedef char* get_clipboard_text_fn(scoped_arena*);

typedef struct work_queue work_queue;
typedef void platform_work_queue_callback_fn(work_queue *Queue, void *Data);
typedef void work_queue_add_entry_fn(work_queue *Queue, platform_work_queue_callback_fn *Callback, void *Data);
typedef void work_queue_complete_all_work_fn(work_queue *Queue);

typedef struct platform_state {
  struct {
    // NOTE(eric): These are expected to be initialized to all zeros
    u8  *PermanentStorage;
    u32 PermanentStorageSize;
    u8  *TransientStorage;
    u32 TransientStorageSize;
    
    v2       WindowDim;
    v2       RenderDim;
    mouse    Mouse;
    keyboard Keyboard;
    b32      InFocus;
    char     Text[32];
    
    work_queue *WorkQueue;
  } Input;
  
  struct {
    b32 IsRunning;
    i32 TargetFPS;
    b32 VSync;
    b32 FullScreen;
    
    audio_buffer AudioBuffer;
  } Shared;
  
  struct {
    get_time_ms_fn                  *GetTimeMs;
    get_opengl_proc_address_fn      *GetOpenGLProcAddress;
    load_entire_file_fn             *LoadEntireFile;
    free_entire_file_fn             *FreeEntireFile;
    log_fn                          *Log;
    set_clipboard_text_fn           *SetClipboardText;
    get_clipboard_text_fn           *GetClipboardText;
    work_queue_add_entry_fn         *WorkQueueAddEntry;
    work_queue_complete_all_work_fn *WorkQueueCompleteAllWork;
  } Interface;
} platform_state;

///////////////////////////////////////////////////////////////////////////////
// game provided state and functions

#include "renderer.h" // renderer struct
#include "shaders.h"
#include "textures.h"
#include "ui/ui.h"
#include "ui/debug_console.h"

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

typedef enum program_mode {
  PROGRAM_MODE_game,
  PROGRAM_MODE_editor
} program_mode;

// game_state defines the state that the game itself stores
typedef struct game_state {
  b32 IsInitialized;
  
  memory_arena PermanentArena;
  memory_arena TransientArena;
  renderer Renderer;
  
  v2 RenderDim; // NOTE(eric): Dimension of framebuffers used for rendering.
  program_mode Mode;
  v2 MousePos;
  v2 MouseClip;
  
  shader_catalog ShaderCatalog;
  texture_catalog TextureCatalog;
  font_manager FontManager;
  
  console Console;

  f32 FPS;
  i32 MCPF;
  u64 FrameStartTime;
  
  // TODO: Move into state-specific structs
  v4 ClearColor;
  f32 AudioTime;
  f32 VideoTime;
  
  i32 TextureOffset;
  f32 Accum;
  
  GLuint AllPurposeVAO;
  
  framebuffer HDRTarget;
  framebuffer FXAATarget;
  
  font TitleFont;
  
  m4x4 ViewProjectionMatrix;
  u32 FontVAO;
  u32 FontVBO;
} game_state;

#endif // GAME_H
