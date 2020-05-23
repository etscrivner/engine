#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
// TODO: Enumerate displays and show window on selected display rather than
// default display.
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

#include <GL/glx.h>

#include "game.h"

#define GAME_LIBRARY "./libgame.so"

typedef struct watched_file {
  const char *FilePath;
  ino_t InodeID;

  struct {
    int ReturnValue;
    int Errno;
  } Error;

  bool WasModified;
} watched_file;

watched_file WatchedFile(const char *FilePath)
{
  struct stat Attr;
  watched_file Result = {
    .FilePath          = FilePath,
    .Error.ReturnValue = stat(FilePath, &Attr),
    .WasModified       = false
  };

  if (Result.Error.ReturnValue == 0)
    Result.InodeID = Attr.st_ino;
  else
    Result.Error.Errno = errno;

  return(Result);
}

bool IsValid(watched_file *WatchedFile)
{
  return(WatchedFile->FilePath != NULL);
}

bool WatchedFileHasError(watched_file *WatchedFile)
{
  return(WatchedFile->Error.ReturnValue != 0);
}

char* WatchedFileGetError(watched_file *WatchedFile)
{
  return(strerror(WatchedFile->Error.Errno));
}

void WatchedFileUpdate(watched_file *WatchedFile)
{
  WatchedFile->WasModified = false;

  struct stat Attr;
  WatchedFile->Error.ReturnValue = stat(WatchedFile->FilePath, &Attr);
  if (WatchedFile->Error.ReturnValue != 0)
  {
    WatchedFile->Error.Errno = errno;
    return;
  }

  if (WatchedFile->InodeID != Attr.st_ino)
  {
    WatchedFile->InodeID = Attr.st_ino;
    WatchedFile->WasModified = true;
  }
}

///////////////////////////////////////////////////////////////////////////////

void GameUpdateStub(platform_state *_Platform, f32 _DeltaTimeSecs) {}
void GameShutdownStub(platform_state *_Platform) {}
void GameOnFrameStartStub(platform_state *_Platform) {}
void GameOnFrameEndStub(platform_state *_Platform) {}

typedef struct game_library {
  void *Handle;
  watched_file LibraryWatcher;

  update_fn *Update;
  shutdown_fn *Shutdown;
  on_frame_start_fn *OnFrameStart;
  on_frame_end_fn *OnFrameEnd;
} game_library;

void GameLibraryOpen(game_library *Game)
{
  bool ForceUpdate = false;

  if (!IsValid(&Game->LibraryWatcher))
  {
    Game->LibraryWatcher = WatchedFile(GAME_LIBRARY);
    if (WatchedFileHasError(&Game->LibraryWatcher))
    {
      fprintf(stderr, "error: file watcher: %s\n", WatchedFileGetError(&Game->LibraryWatcher));
      exit(1);
    }

    // Force update on first pass through this function to ensure we try to
    // load the library before calling functions from it.
    ForceUpdate = true;
  }

  WatchedFileUpdate(&Game->LibraryWatcher);
  if (WatchedFileHasError(&Game->LibraryWatcher))
  {
    fprintf(stderr, "error: file watcher: %s\n", WatchedFileGetError(&Game->LibraryWatcher));
    exit(1);
  }
  
  if (ForceUpdate || Game->LibraryWatcher.WasModified)
  {
    if (Game->Handle)
    {
      dlclose(Game->Handle);
    }

    void* Handle = dlopen(GAME_LIBRARY, RTLD_NOW);
    if (Handle)
    {
      Game->Handle = Handle;
      Game->Update = (update_fn*)dlsym(Game->Handle, "Update");
      Game->Shutdown = (shutdown_fn*)dlsym(Game->Handle, "Shutdown");
      Game->OnFrameStart = (on_frame_start_fn*)dlsym(Game->Handle, "OnFrameStart");
      Game->OnFrameEnd = (on_frame_end_fn*)dlsym(Game->Handle, "OnFrameEnd");
    }
    else
    {
      // NOTE: We need to stub these methods so that between loads when the lib
      // cannot be loaded because the file is partially written etc the
      // platform layer does not crash.
      fprintf(stderr, "warning: library loading: %s\n", dlerror());
      Game->Handle = NULL;
      Game->Update = GameUpdateStub;
      Game->Shutdown = GameShutdownStub;
      Game->OnFrameStart = GameOnFrameStartStub;
      Game->OnFrameEnd = GameOnFrameEndStub;
    }
  }
}

void GameLibraryClose(game_library *Game)
{
  if (Game->Handle)
  {
    dlclose(Game->Handle);
  }
}

///////////////////////////////////////////////////////////////////////////////

// NOTE: Use CLOCK_MONOTONIC_RAW if available as it is not subject to
// adjustment by NTP.
#ifdef CLOCK_MONOTONIC_RAW
#define LINUX_MONOTONIC_CLOCK CLOCK_MONOTONIC_RAW
#else
#define LINUX_MONOTONIC_CLOCK CLOCK_MONOTONIC
#endif

internal u64 LinuxGetTimeMs(void)
{
  struct timespec TimeSpec;
  u64 Result = 0;

  if (clock_gettime(LINUX_MONOTONIC_CLOCK, &TimeSpec) == 0)
  {
    u64 SecsInMs = TimeSpec.tv_sec * 1000.0f; // Converts seconds to MS
    u64 NsInMs = TimeSpec.tv_nsec / 1000000;
    Result = SecsInMs + NsInMs;
  }
  else
  {
    fprintf(stderr, "Linux error: Failed to get time: %s\n", strerror(errno));
    Result = 0.0f;
  }

  return(Result);
}

internal f32 LinuxGetTimeSecs(void)
{
  f64 Result = (LinuxGetTimeMs() / 1000.0f);
  return(Result);
}

internal void LinuxSleep(i32 SleepMS)
{
  struct timespec TimeToSleep, TimeRemaining;
  TimeRemaining.tv_sec = SleepMS / 1000;
  TimeRemaining.tv_nsec = (SleepMS % 1000) * 1000000;

  // NOTE: nanosleep can be interrupted if your application receives a signal
  // it has to handle during its sleep. In order to ensure we finish out our
  // sleep we may have perform monotonically decreasing nanosleeps in a loop.
  i32 WasError;
  do {
    errno = 0;

    TimeToSleep.tv_sec = TimeRemaining.tv_sec;
    TimeToSleep.tv_nsec = TimeRemaining.tv_nsec;

    WasError = nanosleep(&TimeToSleep, &TimeRemaining);
  } while (WasError && (errno == EINTR));
}

///////////////////////////////////////////////////////////////////////////////

internal void PlatformInit(platform_state* Platform)
{
  // Input state to game
  {
    Platform->Input.PermanentStorageSize = PERMANENT_STORAGE_SIZE;
    Platform->Input.PermanentStorage = (u8*)calloc(1, Platform->Input.PermanentStorageSize);
    Platform->Input.TransientStorageSize = TRANSIENT_STORAGE_SIZE;
    Platform->Input.TransientStorage = (u8*)calloc(1, Platform->Input.TransientStorageSize);
  }

  // Shared state
  {
    Platform->Shared.IsRunning = true;
    Platform->Shared.TargetFPS = 60.0f;
    Platform->Shared.VSync = true;
    Platform->Shared.FullScreen = false;
  }

  // Interfaces
  {
    Platform->Interface.GetTimeSecs = LinuxGetTimeSecs;
  }
}

internal void PlatformDestroy(platform_state* Platform)
{
  free(Platform->Input.PermanentStorage);
  free(Platform->Input.TransientStorage);
}

int main() {
  i32 ExitCode = 0;
  game_library GameLibrary = {};
  platform_state Platform = {};

  GameLibraryOpen(&GameLibrary);
  PlatformInit(&Platform);

  ///////////////////////////////////////////////////////////////////////////////

  // Desired OpenGL video mode
  GLint DesiredVideoMode[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };

  // Open connection to the default XWindows service
  printf("Initializing X11\n");
  Display *CurrentDisplay = XOpenDisplay(NULL);
  if (CurrentDisplay != NULL)
  {
    printf("\tDisplay: %s (%d)\n", XDisplayString(CurrentDisplay), ScreenCount(CurrentDisplay));
    printf("\tVendor:  %s\n", XServerVendor(CurrentDisplay));
    printf("\tRelease: %d\n", XVendorRelease(CurrentDisplay));

    // Attempt to provision visuals for OpenGL
    Window Root = DefaultRootWindow(CurrentDisplay);
    XVisualInfo *GLVisual = glXChooseVisual(CurrentDisplay, 0, DesiredVideoMode);
    if (GLVisual != NULL)
    {
      // Create color map
      Colormap GLColorMap = XCreateColormap(CurrentDisplay, Root, GLVisual->visual, AllocNone);

      XSetWindowAttributes WindowAttribs;
      WindowAttribs.colormap = GLColorMap;
      WindowAttribs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask;

      // Create a window on the default screen
      i32 DefaultScreen = XDefaultScreen(CurrentDisplay);
      Window CurrentWindow = XCreateWindow(
        CurrentDisplay,
        Root,
        0, 0, 800, 600, /* x, y, w, h */
        0, /* border width */
        GLVisual->depth,
        InputOutput, /*class */
        GLVisual->visual,
        CWColormap | CWEventMask,
        &WindowAttribs
      );

      // Map window, set title, icon etc...
      XMapRaised(CurrentDisplay, CurrentWindow);
      XStoreName(CurrentDisplay, CurrentWindow, "Plague 2.0");
      XSync(CurrentDisplay, False);

      // Create OpenGL context
      XWindowAttributes EventWindowAttrs;
      GLXContext GLCtx = glXCreateContext(CurrentDisplay, GLVisual, NULL, GL_TRUE);
      if (GLCtx != NULL)
      {
        glXMakeCurrent(CurrentDisplay, CurrentWindow, GLCtx);

        XEvent Event;
        b32 Running = true;
        while (Running) {
          // Process input
          while (XCheckMaskEvent(CurrentDisplay, -1, &Event)) {
            if (Event.type == Expose) {
              XGetWindowAttributes(CurrentDisplay, CurrentWindow, &EventWindowAttrs);
              glViewport(0, 0, EventWindowAttrs.width, EventWindowAttrs.height);
            } else if (Event.type == KeyPress || Event.type == KeyRelease) {
              if (Event.xkey.keycode == XKeysymToKeycode(CurrentDisplay, XK_Escape) /* ESC */) {
                Running = false;
              }
            }
          }

          // Update
          GameLibrary.Update(&Platform, 0.0f);

          // Render
          glXSwapBuffers(CurrentDisplay, CurrentWindow);
        }

        glXMakeCurrent(CurrentDisplay, None, NULL);
        glXDestroyContext(CurrentDisplay, GLCtx);
      }
      else
      {
        fprintf(stderr, "GLX error: Failed to create OpenGL context. \n");
        ExitCode = 1;
      }

      XFree(GLVisual);
      XDestroyWindow(CurrentDisplay, CurrentWindow);
    }
    else
    {
      fprintf(stderr, "GLX error: Failed to find suitable visual.\n");
      ExitCode = 1;
    }

    
    XCloseDisplay(CurrentDisplay);
  }
  else
  {
    fprintf(stderr, "Failed to open connection to default XWindows server.\n");
    ExitCode = 1;
  }

  GameLibraryClose(&GameLibrary);
  PlatformDestroy(&Platform);
  
  return(ExitCode);
}
