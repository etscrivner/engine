#ifndef GAME_H
#define GAME_H

#include "common/language_layer.h"

#ifdef PLATFORM_MACOS
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#endif

// Game options
#define DEFAULT_TARGET_FPS 60.0f
#define PERMANENT_STORAGE_SIZE Megabytes(256)
#define TRANSIENT_STORAGE_SIZE Megabytes(256)

// Platform provided functions
typedef f32 get_time_secs_fn(void);

// platform_state defines the state that the game expects to receive from the
// platform on update.
typedef struct platform_state {
  struct {
    u8  *PermanentStorage;
    u32 PermanentStorageSize;
    u8  *TransientStorage;
    u32 TransientStorageSize;
  } Input;

  struct {
    b32 IsRunning;
    i32 TargetFPS;
    b32 VSync;
    b32 FullScreen;
  } Shared;

  struct {
    get_time_secs_fn *GetTimeSecs;
  } Interface;
} platform_state;

// Game-provided functions
//

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
  
} game_state;

#endif // GAME_H
