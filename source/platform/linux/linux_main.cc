#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/mman.h>

// NOTE: Linux is a weird platform with lots of non-obvious, poorly documented
// methods for getting at certain functionality. Luckily, quite a few engines
// and platform-layers have already done this work. Useful references a listed
// below:
//
// Godot Engine X11 layer:
// https://github.com/godotengine/godot/blob/67682b35b0e3057b2d630592815cd84596e741e3/platform/x11/os_x11.cpp

// TODO(eric): Explore using XCB instead of Xlib (some perf benefits because
// not all calls are synchronous in XCB)-- is it worth it?

// X11 (Window Manager)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

// TODO: Enumerate displays and show window on selected display rather than
// default display.
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

// OpenGL (Hardware-accelerated Graphics)
#include <GL/glx.h>
#include "ext/glxext.h"

// Sempahores
#include <semaphore.h>

#define ALSA_PCM_NEW_HW_PARAMS_API // Use the new ALSA API
#include <alsa/asoundlib.h>

#include "game.h"

#define HandleButtonPress(ButtonArray, Item, IsDown, IsARepeat) do {    \
    (ButtonArray)[(Item)].Pressed = (IsDown);                           \
    (ButtonArray)[(Item)].Down = (IsDown);                              \
    (ButtonArray)[(Item)].IsRepeat = (IsARepeat);                       \
  } while(0)

#define GAME_LIBRARY "./libgame.so"
#define GAME_LIBRARY_LOCKFILE "./build.lock"

void GameUpdateStub(platform_state *_Platform, f32 _DeltaTimeSecs) {}
void GameShutdownStub(platform_state *_Platform) {}
void GameOnFrameStartStub(platform_state *_Platform) {}
void GameOnFrameEndStub(platform_state *_Platform) {}

typedef struct work_queue work_queue;
typedef void work_queue_callback_fn(work_queue *Queue, void *Data);

internal u64  LinuxGetTimeMs(void);
internal void LinuxSleep(i32 SleepMS);

internal f32   LinuxGetTimeSecs(void);
internal void* LinuxGetOpenGLProcAddress(const char *ProcName);
internal b32   LinuxLoadEntireFile(const char *FileName, platform_entire_file *FileOutput);
internal void  LinuxFreeEntireFile(platform_entire_file *File);
internal void  LinuxLog(const char *Format, ...);
internal b32   LinuxSetClipboardText(const char *Text);
internal char* LinuxGetClipboardText(scoped_arena* ScopedArena);
internal void  LinuxWorkQueueAddEntry(work_queue *Queue, work_queue_callback_fn *Callback, void *UserData);
internal void  LinuxWorkQueueCompleteAllWork(work_queue *Queue);

// Linux layer specific files
#include "linux_audio.cc"

///////////////////////////////////////////////////////////////////////////////

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
  local_persist bool WasLockFile = false;
  bool ForceUpdate = (Game->Handle == NULL);
  
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
    fprintf(stderr, "error: file watcher (%s): %s\n", Game->LibraryWatcher.FilePath, WatchedFileGetError(&Game->LibraryWatcher));
    exit(1);
  }
  
  // Guard against reloading if lockfile is present
  if (access(GAME_LIBRARY_LOCKFILE, F_OK) != -1)
  {
    WasLockFile = true;
    return;
  }
  
  if (ForceUpdate || Game->LibraryWatcher.WasModified || WasLockFile)
  {
    bool IsReload = false;
    
    if (WasLockFile)
    {
      IsReload = true;
      WasLockFile = false;
    }
    
    if (Game->Handle != NULL)
    {
      IsReload = true;
      dlclose(Game->Handle);
      Game->Handle = NULL;
    }
    
    void* Handle = dlopen(GAME_LIBRARY, RTLD_GLOBAL | RTLD_NOW);
    if (Handle)
    {
      fprintf(stderr, "info: successfully reloaded %s\n", GAME_LIBRARY);
      Game->Handle = Handle;
      Game->Update = (update_fn*)dlsym(Game->Handle, "Update");
      Game->Shutdown = (shutdown_fn*)dlsym(Game->Handle, "Shutdown");
      Game->OnFrameStart = (on_frame_start_fn*)dlsym(Game->Handle, "OnFrameStart");
      Game->OnFrameEnd = (on_frame_end_fn*)dlsym(Game->Handle, "OnFrameEnd");
    }
    else
    {
      Game->Handle = NULL;
      fprintf(stderr, "warning: library loading: %s\n", dlerror());
      // NOTE: We need to stub these methods so that between loads when the lib
      // cannot be loaded because the file is partially written etc the
      // platform layer does not crash.
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

internal u64 LinuxGetTimeMicros(void)
{
  struct timespec TimeSpec;
  u64 Result = 0;
  
  if (clock_gettime(LINUX_MONOTONIC_CLOCK, &TimeSpec) == 0)
  {
    u64 SecsInMicros = TimeSpec.tv_sec * 1000000.0f; // Converts seconds to microseconds
    u64 NsInMicros = TimeSpec.tv_nsec / 1000;
    Result = SecsInMicros + NsInMicros;
  }
  else
  {
    fprintf(stderr, "Linux error: Failed to get time: %s\n", strerror(errno));
    Result = 0.0f;
  }
  
  return(Result);
}

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
  f32 Result = (LinuxGetTimeMs() / 1000.0f);
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

internal void* LinuxGetOpenGLProcAddress(const char *ProcName)
{
  return (void*)glXGetProcAddressARB((const GLubyte*)ProcName);
}

internal b32 LinuxLoadEntireFile(const char *FileName, platform_entire_file *FileOutput)
{
  b32 Result = false;
  FILE *File = fopen(FileName, "rb");
  
  if (File != NULL) {
    fseek(File, 0, SEEK_END);
    FileOutput->SizeBytes = ftell(File);
    fseek(File, 0, SEEK_SET);
    
    FileOutput->Data = (u8*)calloc(1, FileOutput->SizeBytes + 1);
    fread(FileOutput->Data, FileOutput->SizeBytes, 1, File);
    FileOutput->Data[FileOutput->SizeBytes] = '\0';
    
    fclose(File);
    Result = true;
  }
  
  return(Result);
}

internal void LinuxFreeEntireFile(platform_entire_file *File)
{
  free(File->Data);
  File->SizeBytes = 0;
}

internal void LinuxLog(const char *Format, ...)
{
  va_list Args;
  va_start(Args, Format);
  vfprintf(stderr, Format, Args);
  va_end(Args);
}

///////////////////////////////////////////////////////////////////////////////

// X11 OpenGL Extension Procs
#define GLXLoadExtension(Type, Name) Name = (PFNGLX##Type##PROC)glXGetProcAddressARB((const GLubyte*)#Name)
#define GLXLoadRequiredExtension(ProcName, Type, ExtName)       \
Assert(ExtensionInList(GLXExtensionList, #ExtName));          \
GLXLoadExtension(Type, ProcName)

#define GLXProc(Type, Name) PFNGLX##Type##PROC glX##Name = NULL;

// glX procs we care about
GLXProc(SWAPINTERVALMESA, SwapIntervalMESA)
GLXProc(SWAPINTERVALEXT, SwapIntervalEXT)
GLXProc(CREATECONTEXTATTRIBSARB, CreateContextAttribsARB)
///////////////////////////////////////////////////////////////////////////////

static Atom NetWMIconAtom = None;
static Atom WMDeleteWindowAtom = None;
// See:
//   * https://www.jwz.org/doc/x-cut-and-paste.html
//   * https://www.uninformativ.de/blog/postings/2017-04-02/0/POSTING-en.html
static Atom ClipboardAtom = None;
static Atom PrimaryAtom = None;
static Atom UTF8StringAtom = None;
static Atom TargetsAtom;
static Atom PlatformSelectionReadAtom = None;
static Atom PlatformTargetPropertyAtom = None;

static Display *GlobalDisplay = NULL;
static Window GlobalWindow = None;
XIM GlobalXIM = None;
XIC GlobalXIC = None;
static b32 GlobalSelectionWaiting = false;
static platform_state GlobalPlatform = {};
static u32 GlobalTextPos = 0;

enum
{
  _NET_WM_STATE_REMOVE = 0,
  _NET_WM_STATE_ADD = 1,
  _NET_WM_STATE_TOGGLE = 2
};

internal void X11LoadAtoms(Display *CurrentDisplay)
{
  NetWMIconAtom = XInternAtom(CurrentDisplay, "_NET_WM_ICON", False);
  WMDeleteWindowAtom = XInternAtom(CurrentDisplay, "WM_DELETE_WINDOW", False);
  ClipboardAtom = XInternAtom(CurrentDisplay, "CLIPBOARD", False);
  PrimaryAtom = XInternAtom(CurrentDisplay, "PRIMARY", False);
  UTF8StringAtom = XInternAtom(CurrentDisplay, "UTF8_STRING", False);
  TargetsAtom = XInternAtom(CurrentDisplay, "TARGETS", False);
  
  PlatformSelectionReadAtom = XInternAtom(CurrentDisplay, "PLAGUE_X11_SELECTION", False);
  PlatformTargetPropertyAtom = XInternAtom(CurrentDisplay, "PLAGUE_X11_TARGET", False);
}

internal void X11SetWindowTitle(char* Title, Display *CurrentDisplay, Window CurrentWindow)
{
  // Set window class name (for menu bar, alt-tab, etc...)
  XWMHints *WMHints = XAllocWMHints();
  WMHints->input = True;
  WMHints->flags = InputHint;
  
  XClassHint *ClassHint = XAllocClassHint();
  ClassHint->res_name = Title;
  ClassHint->res_class = Title;
  
  XSetWMProperties(CurrentDisplay, CurrentWindow, NULL, NULL, NULL, 0, NULL, WMHints, ClassHint);
  XFree(WMHints);
  XFree(ClassHint);
  
  // Set window title bar name
  XStoreName(CurrentDisplay, CurrentWindow, Title);
}

internal b32 X11SetWindowIconPNG(const char *PNGFile, Display *CurrentDisplay, Window CurrentWindow)
{
  b32 Result = false;
  
  i32 IconWidth, IconHeight, Channels;
  u8 *IconData = stbi_load(PNGFile, &IconWidth, &IconHeight, &Channels, 0);
  if (IconData)
  {
    // NOTE(eric): Add 2 to include icon width and height.
    i32 PropSize = 2 + (IconWidth * IconHeight);
    
    // NOTE(eric): Need to use the platform `long` type here which is 32-bits
    // or 64-bits depending on the architecture.
    long *PropData = (long*)calloc(1, PropSize * sizeof(long));
    PropData[0] = IconWidth;
    PropData[1] = IconHeight;
    
    // Translate RGBA format to ARGB format used for icons
    u8 *Src = IconData;
    long *Dst = PropData + 2;
    for (i32 I = 0; I < IconWidth * IconHeight; ++I)
    {
      *Dst++ = (Src[I * 4 + 0] << 16) | (Src[I * 4 + 1] << 8) | (Src[I * 4 + 2] << 0) | (Src[I * 4 + 3] << 24);
    }
    
    XChangeProperty(CurrentDisplay,
                    CurrentWindow,
                    NetWMIconAtom,
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (u8*)PropData,
                    PropSize);
    
    free(PropData);
    stbi_image_free(IconData);
    
    XFlush(CurrentDisplay);
    Result = true;
  }
  else
  {
    fprintf(stderr, "error: failed to load icon image: %s\n", PNGFile);
  }
  
  return(Result);
}

internal v2u X11GetDrawableAreaSize(Display *CurrentDisplay, Window CurrentWindow)
{
  i32 XPos, YPos;
  u32 Width, Height, BorderWidth, Depth;
  Window RootWnd;
  XGetGeometry(CurrentDisplay, CurrentWindow, &RootWnd, &XPos, &YPos, &Width, &Height, &BorderWidth, &Depth);
  
  return(V2U(Width, Height));
}

internal void X11ToggleAllowResizing(b32 AllowResizing)
{
  // NOTE: Disable window resizing. Tiling window managers will freely
  // ignore this but at their own peril as it make break visuals for our game.
  XSizeHints *NoResizingHints = XAllocSizeHints();
  NoResizingHints->flags = 0;
  if (!AllowResizing) {
    // NOTE: To make window resizable again simply pass in size hints without
    // these flags or values set.
    NoResizingHints->flags = PMinSize | PMaxSize;
    NoResizingHints->min_width = DEFAULT_WINDOW_WIDTH;
    NoResizingHints->max_width = DEFAULT_WINDOW_WIDTH;
    NoResizingHints->min_height = DEFAULT_WINDOW_HEIGHT;
    NoResizingHints->max_height = DEFAULT_WINDOW_HEIGHT;
  }

  XSetWMNormalHints(GlobalDisplay, GlobalWindow, NoResizingHints);
  XFree(NoResizingHints);
}

internal b32 X11PumpEvents(platform_state *Platform)
{
  local_persist XEvent PreviousEvent = {};
  local_persist XEvent Event = {};
  local_persist XWindowAttributes EventWindowAttrs;
  b32 Result = false;
  
  if (XPending(GlobalDisplay))
  {
    Result = true;
    XNextEvent(GlobalDisplay, &Event);
    
    // NOTE: This is needed for IMEs to hook keypresses in some cases.
    if (XFilterEvent(&Event, None) == True)
    {
      return(false);
    }
    
    switch (Event.type) {
      case ClientMessage:
      {
        // NOTE(eric): Handle users clicking the close button
        if ((Atom)Event.xclient.data.l[0] == WMDeleteWindowAtom)
        {
          Platform->Shared.IsRunning = false;
        }
      }
      break;
      case Expose:
      {
        XGetWindowAttributes(GlobalDisplay, GlobalWindow, &EventWindowAttrs);
        // NOTE: Window dimensions will be different from the drawable area 
        // size. The drawable area size is the units that that mouse position 
        // is actually reported in.
        Platform->Input.WindowDim = V2U(EventWindowAttrs.width, EventWindowAttrs.height);
        Platform->Input.RenderDim = X11GetDrawableAreaSize(GlobalDisplay, GlobalWindow);
        
        // Fetch initial mouse position on window exposure so we
        // don't have to wait for the first mouse movement by the
        // player.
        Window Root, Child;
        i32 RootX, RootY, XPos, YPos;
        u32 ButtonMasks;
        XQueryPointer(GlobalDisplay,
                      GlobalWindow,
                      &Root, &Child,
                      &RootX, &RootY,
                      &XPos, &YPos,
                      &ButtonMasks);
        
        Platform->Input.Mouse.Pos = V2I(XPos, Platform->Input.RenderDim.Height - YPos);
        Platform->Input.Mouse.Pos01 = V2(
          Clamp01((f32)Platform->Input.Mouse.Pos.X / (f32)Platform->Input.RenderDim.Width),
          Clamp01((f32)Platform->Input.Mouse.Pos.Y / (f32)Platform->Input.RenderDim.Height)
        );
      }
      break;
      case ResizeRequest:
      {
        XGetWindowAttributes(GlobalDisplay, GlobalWindow, &EventWindowAttrs);
        Platform->Input.WindowDim = V2U(EventWindowAttrs.width, EventWindowAttrs.height);
        Platform->Input.RenderDim = X11GetDrawableAreaSize(GlobalDisplay, GlobalWindow);
        glViewport(0, 0, Platform->Input.RenderDim.Width, Platform->Input.RenderDim.Height);
      }
      break;
      case MotionNotify:
      {
        Platform->Input.Mouse.Pos = V2I(Event.xmotion.x, Platform->Input.RenderDim.Height - Event.xmotion.y);
        Platform->Input.Mouse.Pos01 = V2(
          Clamp01((f32)Platform->Input.Mouse.Pos.X / (f32)Platform->Input.RenderDim.Width),
          Clamp01((f32)Platform->Input.Mouse.Pos.Y / (f32)Platform->Input.RenderDim.Height)
        );
      }
      break;
      case ButtonPress:
      case ButtonRelease:
      {
        b32 IsDown = (Event.type == ButtonPress);
        
        Platform->Input.Mouse.Pos = V2I(Event.xmotion.x, Platform->Input.RenderDim.Height - Event.xmotion.y);
        Platform->Input.Mouse.Pos01 = V2(
          Clamp01((f32)Platform->Input.Mouse.Pos.X / (f32)Platform->Input.RenderDim.Width),
          Clamp01((f32)Platform->Input.Mouse.Pos.Y / (f32)Platform->Input.RenderDim.Height)
        );
        
        if (Event.xbutton.button == Button1)
        {
          HandleButtonPress(Platform->Input.Mouse.Button, MOUSE_BUTTON_left, IsDown, false);
        }
        if (Event.xbutton.button == Button2)
        {
          HandleButtonPress(Platform->Input.Mouse.Button, MOUSE_BUTTON_middle, IsDown, false);
        }
        if (Event.xbutton.button == Button3)
        {
          HandleButtonPress(Platform->Input.Mouse.Button, MOUSE_BUTTON_right, IsDown, false);
        }

        // NOTE: No specific mouse wheel event exists in Xlib, but it is
        // convention to emit vertical wheel motion as 4 (up) and 5 (down) and
        // horizontal wheel motion as 6 (left) and 7 (right).
        switch(Event.xbutton.button) {
        case 4: // Scroll wheel up
        {
          Platform->Input.Mouse.Wheel.Y = 1;
        }
        break;
        case 5: // Scroll wheel down
        {
          Platform->Input.Mouse.Wheel.Y = -1;
        }
        break;
        case 6: // Left
        {
          Platform->Input.Mouse.Wheel.X = -1;
        }
        break;
        case 7: // Right
        {
          Platform->Input.Mouse.Wheel.X = 1;
        }
        break;
        default: break;
        }
      }
      break;
      case KeyPress:
      case KeyRelease:
      {
        b32 IsDown = (Event.type == KeyPress);
        
        // (Event.xkey.state & ShiftMask) ? 1 : 0
        KeySym Sym = XkbKeycodeToKeysym(GlobalDisplay, Event.xkey.keycode, 0, 1);
        
        // NOTE(eric): Bit of a hack, but there is a straightforward
        // mapping of some keysyms to ASCII character codes. These
        // will likely never change since they are baked into so many
        // Xlib programs.
        u8 AsciiKeyCode = (char)Sym;
        
        // Check whether or not this is a key repeat
        b32 IsKeyRepeat = false;
        if (IsDown)
        {
          IsKeyRepeat = (PreviousEvent.type == KeyRelease &&
                         PreviousEvent.xkey.time == Event.xkey.time &&
                         PreviousEvent.xkey.keycode == Event.xkey.keycode);
        }
        
        // Read input on keypress events
        if (IsDown)
        {
          // TODO: Clean this up a bit and do better bounds checking on the
          // text input strings.
          //
          // Use:
          // https://gist.github.com/baines/5a49f1334281b2685af5dcae81a6fa8a
          char Temp[32];
          Status LookupStatus;
          Xutf8LookupString(GlobalXIC, &Event.xkey, Temp, sizeof(Temp) - 1, &Sym, &LookupStatus);
          if (LookupStatus == XBufferOverflow) {
            // An IME was probably used, and wants to commit more than 32 chars.
            // For now ignore this unlikely case and fail with an assertion
            // error.
            Assert(0 && "IME Buffer Overflow");
          } else if (LookupStatus == XLookupChars) {
            if (Sym != XK_BackSpace && Sym != XK_Delete && Sym != XK_Escape) {
              Assert(GlobalTextPos + strlen(Temp) < 31);
              strncat(Platform->Input.Text, Temp, 31);
              GlobalTextPos += strlen(Temp);
            }
          } else if (LookupStatus == XLookupBoth) {
            if (Sym != XK_BackSpace && Sym != XK_Delete && Sym != XK_Escape) {
              Assert(GlobalTextPos + strlen(Temp) < 31);
              strncat(Platform->Input.Text, Temp, 31);
              GlobalTextPos += strlen(Temp);
            }
          } else if (LookupStatus == XLookupKeySym) {
            //printf("info: X11 Input Manager got XLookupKeySym\n");
          }
        }
        
        if (Sym == XK_Escape) // Escape
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_esc, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_BackSpace) // Backspace
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_backspace, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_Delete) // Delete
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_delete, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_Tab) // Tab
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_tab, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_Return) // Enter/Return
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_enter, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_Control_L || Sym == XK_Control_R) // Control
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_ctrl, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_Meta_L || Sym == XK_Meta_R || Sym == XK_Alt_L || Sym == XK_Alt_R) // Alt/Meta
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_alt, IsDown, IsKeyRepeat);
        }
        else if (Sym == XK_Shift_L || Sym == XK_Shift_R) // Shift
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_shift, IsDown, IsKeyRepeat);
        }
        else if (Sym >= XK_Left && Sym <= XK_Down) // Arrow keys (Left, Up, Right, Down)
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_left + (Sym - XK_Left), IsDown, IsKeyRepeat);
        }
        else if (AsciiKeyCode >= 0x20 && AsciiKeyCode <= 0x40) // Space - @ (Includes 0-9)
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_space + (AsciiKeyCode - 0x20), IsDown, IsKeyRepeat);
        }
        else if (AsciiKeyCode >= 0x41 && AsciiKeyCode <= 0x60) // a-z to `
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_a + (AsciiKeyCode - 0x41), IsDown, IsKeyRepeat);
        }
        else if (AsciiKeyCode >= 0x7B && AsciiKeyCode <= 0x7E)
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_lbracket + (AsciiKeyCode - 0x7B), IsDown, IsKeyRepeat);
        }
        else if (AsciiKeyCode >= 0xBE && AsciiKeyCode <= 0xC9) // F1 - F12
        {
          HandleButtonPress(Platform->Input.Keyboard.Key, KEY_f1 + (AsciiKeyCode - 0xBE), IsDown, IsKeyRepeat);
        }
        else
        {
          printf("Unhandled Key: 0x%02X\n", AsciiKeyCode);
        }
      }
      break;
      case EnterNotify:
      {
        Platform->Input.InFocus = true;
      }
      break;
      case LeaveNotify:
      {
        Platform->Input.InFocus = false;
      }
      break;
      case FocusIn:
      {
        Platform->Input.InFocus = true;
      }
      break;
      case FocusOut:
      {
        Platform->Input.InFocus = false;
      }
      break;
      case SelectionRequest:
      {
        // NOTE: Send clipboard data to remote application that is requesting it.
        const XSelectionRequestEvent *Request = &Event.xselectionrequest;
        XEvent SendEvent;
        int SelectionFormat;
        unsigned long NumBytes;
        unsigned long Overflow;
        unsigned char *SelectionData;
        
        ClearMemory(SendEvent);
        SendEvent.xany.type = SelectionNotify;
        SendEvent.xselection.selection = Request->selection;
        SendEvent.xselection.target = None;
        SendEvent.xselection.property = None;
        SendEvent.xselection.requestor = Request->requestor;
        SendEvent.xselection.time = Request->time;
        
        if (XGetWindowProperty(GlobalDisplay, DefaultRootWindow(GlobalDisplay),
                               PlatformSelectionReadAtom, 0, INT_MAX/4, False, Request->target,
                               &SendEvent.xselection.target, &SelectionFormat, &NumBytes,
                               &Overflow, &SelectionData) == Success)
        {
          if (SendEvent.xselection.target == Request->target) {
            XChangeProperty(
                            GlobalDisplay, Request->requestor, Request->property,
                            SendEvent.xselection.target, SelectionFormat, PropModeReplace,
                            SelectionData, NumBytes
                            );
            SendEvent.xselection.property = Request->property;
          } else if (TargetsAtom == Request->target) {
            Atom SupportedFormats[] = { TargetsAtom, SendEvent.xselection.target };
            XChangeProperty(
                            GlobalDisplay, Request->requestor, Request->property,
                            XA_ATOM, 32, PropModeReplace,
                            (u8*)SupportedFormats,
                            ArrayCount(SupportedFormats)
                            );
            SendEvent.xselection.property = Request->property;
            SendEvent.xselection.target = TargetsAtom;
          }
          
          XFree(SelectionData);
        }
        
        XSendEvent(GlobalDisplay, Request->requestor, False, 0, &SendEvent);
        XSync(GlobalDisplay, False);
      }
      break;
      case SelectionNotify:
      {
        // NOTE: Let our application now that clipboard data from elsewhere is
        // now available.
        GlobalSelectionWaiting = false;
      }
      break;
      // NOTE: This event is received when another window becomes selection owner
      // using XSetSelectionOwner
      case SelectionClear:
      // TODO: Implement selection clear handling for clipboard
      printf("SelectionClear\n");
      break;
      default: break;
    }
    
    PreviousEvent = Event;
  }
  
  return(Result);
}

internal void LinuxSetVSync(b32 VSync)
{
  if (glXSwapIntervalEXT)
  {
    glXSwapIntervalEXT(GlobalDisplay, GlobalWindow, VSync ? 1 : 0);
  }
  else if (glXSwapIntervalMESA)
  {
    glXSwapIntervalMESA(VSync ? 1 : 0);
  }
}

internal b32 LinuxSetClipboardText(const char *Text)
{
  b32 Result = true;
  
  XChangeProperty(
    GlobalDisplay, DefaultRootWindow(GlobalDisplay),
    PlatformSelectionReadAtom, UTF8StringAtom, 8, PropModeReplace,
    (const u8*)Text, strlen(Text)
  );
  
  if (XGetSelectionOwner(GlobalDisplay, ClipboardAtom) != GlobalWindow)
  {
    XSetSelectionOwner(GlobalDisplay, ClipboardAtom, GlobalWindow, CurrentTime);
  }
  
  if (XGetSelectionOwner(GlobalDisplay, PrimaryAtom) != GlobalWindow)
  {
    XSetSelectionOwner(GlobalDisplay, PrimaryAtom, GlobalWindow, CurrentTime);
  }
  
  return(Result);
}

internal char* LinuxGetClipboardText(scoped_arena* ScopedArena)
{
  Window Owner = XGetSelectionOwner(GlobalDisplay, ClipboardAtom);
  Atom Selection;
  char *Result = NULL;
  
  if (Owner == GlobalWindow) {
    Owner = DefaultRootWindow(GlobalDisplay);
    Selection = PlatformSelectionReadAtom;
  } else {
    Owner = GlobalWindow;
    Selection = PlatformTargetPropertyAtom;
    XConvertSelection(GlobalDisplay, ClipboardAtom, UTF8StringAtom, PlatformTargetPropertyAtom, GlobalWindow, CurrentTime);
    XFlush(GlobalDisplay);
    
    GlobalSelectionWaiting = true;
    f32 StartTime = LinuxGetTimeSecs();
    f32 WaitElapsed;
    while (GlobalSelectionWaiting) {
      X11PumpEvents(&GlobalPlatform);
      WaitElapsed = LinuxGetTimeSecs() - StartTime;
      if (WaitElapsed > 1.0f) {
        GlobalSelectionWaiting = false;
        fprintf(stderr, "error: clipboard timeout\n");
        
        // NOTE: We need to set the clipboard text so that next time we won't
        // timeout, otherwise we will hang on every call to this function.
        LinuxSetClipboardText("");
        return(ScopedArenaStrdup(ScopedArena, ""));
      }
    }
  }
  
  Atom ReturnType;
  int ReturnFormat;
  unsigned long NumBytes, Overflow;
  u8 *Source = NULL;
  
  if (XGetWindowProperty(
                         GlobalDisplay, Owner, Selection, 0, INT_MAX/4, False, UTF8StringAtom,
                         &ReturnType, &ReturnFormat, &NumBytes, &Overflow, &Source) == Success)
  {
    if (ReturnType == UTF8StringAtom)
    {
      Result = ScopedArenaPushArray(ScopedArena, NumBytes+1, char);
      memcpy(Result, Source, NumBytes);
      Result[NumBytes] = '\0';
    }
    
    XFree(Source);
  }
  
  if (!Result)
  {
    Result = ScopedArenaStrdup(ScopedArena, "");
  }
  
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////

#define WORKER_THREAD_COUNT 1

typedef struct work_queue_entry {
  work_queue_callback_fn *Callback;
  void *UserData;
} work_queue_entry;

typedef struct work_queue {
  sem_t *Semaphore;
  
  u32 volatile CompletionGoal;
  u32 volatile CompletionCount;
  u32 volatile NextEntryToWrite;
  u32 volatile NextEntryToRead;
  
  work_queue_entry Entries[256];
} work_queue;

typedef struct worker_thread_info {
  i32 ThreadIndex;
  work_queue *Queue;
  GLXContext OpenGLContext;
  thread_atomic_int_t *ExitFlag;
} worked_thread_info;

internal void LinuxWorkQueueAddEntry(work_queue *Queue, work_queue_callback_fn *Callback, void *UserData)
{
  // Assert that we aren't about to overwrite an existing entry. We should fail
  // and triage if this happens.
  u32 NewNextEntryToWrite = (Queue->NextEntryToWrite + 1) % ArrayCount(Queue->Entries);
  Assert(NewNextEntryToWrite != Queue->NextEntryToRead);
  work_queue_entry *Entry = Queue->Entries + Queue->NextEntryToWrite;
  Entry->Callback = Callback;
  Entry->UserData = UserData;
  ++Queue->CompletionGoal;
  
  // Write barrier
  asm volatile("" ::: "memory");
  
  Queue->NextEntryToWrite = NewNextEntryToWrite;
  sem_post(Queue->Semaphore);
}

internal b32 DoNextWorkQueueEntry(work_queue *Queue)
{
  b32 ShouldWaitForWork = false;
  
  u32 OriginalNextEntryToRead = Queue->NextEntryToRead;
  u32 NewNextEntryToRead = (OriginalNextEntryToRead + 1) % ArrayCount(Queue->Entries);
  if (OriginalNextEntryToRead != Queue->NextEntryToWrite)
  {
    // NOTE: Verify that we are getting the entry index we expect, if so then
    // increment the next entry index. This prevents two consumers from a race
    // condition where one can go off the end of the entries. This is due to
    // our single-producer, multiple-consumer threading setup.
    u32 Index = AtomicCompareAndExchangeU32(&Queue->NextEntryToRead,
                                            NewNextEntryToRead,
                                            OriginalNextEntryToRead);
    if (Index == OriginalNextEntryToRead)
    {
      work_queue_entry Entry = Queue->Entries[Index];
      // TODO: Add thread-specific struct that contains scratch arena for the
      // thread that it can use for all its temporary work.
      Entry.Callback(Queue, Entry.UserData);
      AtomicAddU32(&Queue->CompletionCount, 1);
    }
  }
  else
  {
    // NOTE: Only wait for work here as otherwise we either:
    //   1. Did some work
    //   2. Tried to do some work, but got beaten to it. So we should try again 
    //      since we don't know the state of the queue.
    ShouldWaitForWork = true;
  }
  
  return(ShouldWaitForWork);
}

internal void LinuxWorkQueueCompleteAllWork(work_queue *Queue)
{
  while (Queue->CompletionGoal != Queue->CompletionCount)
  {
    DoNextWorkQueueEntry(Queue);
  }
  
  Queue->CompletionGoal = 0;
  Queue->CompletionCount = 0;
}

i32 WorkerThreadLoop(void *UserData)
{
  i32 Result = 0;
  worker_thread_info *ThreadInfo = (worker_thread_info*)UserData;
  
  // NOTE: We need an OpenGL context before we can do anything in our threads.
  // Otherwise, we will get a segfault the second we try to perform OpenGL
  // calls.
  if (!glXMakeContextCurrent(GlobalDisplay, GlobalWindow, GlobalWindow, ThreadInfo->OpenGLContext))
  {
    fprintf(stderr, "Thread: glXMakeContextCurrent failed.\n");
  }
  
  while (thread_atomic_int_load(ThreadInfo->ExitFlag) == 0)
  {
    if (DoNextWorkQueueEntry(ThreadInfo->Queue))
    {
      sem_trywait(ThreadInfo->Queue->Semaphore);
    }
  }
  
  return(Result);
}

///////////////////////////////////////////////////////////////////////////////

internal void PlatformEndFrameReset(platform_state* Platform)
{
  // Reset mouse and keyboard state
  {
    // Clear single-frame pressed boolean value
    for (u32 I = 0; I < MOUSE_BUTTON_MAX; ++I)
    {
      Platform->Input.Mouse.Button[I].Pressed = false;
    }
    
    for (u32 I = 0; I < KEY_MAX; ++I)
    {
      Platform->Input.Keyboard.Key[I].Pressed = false;
    }
    
    // Clear input text
    Platform->Input.Text[0] = '\0';
    GlobalTextPos = 0;

    // Clear mouse wheel
    Platform->Input.Mouse.Wheel = V2I(0, 0);
  }
}

internal void PlatformInit(platform_state* Platform)
{
  // Input state to game
  {
    Platform->Input.PermanentStorageSize = PERMANENT_STORAGE_SIZE;
    Platform->Input.PermanentStorage = (u8*)calloc(1, Platform->Input.PermanentStorageSize);
    Platform->Input.TransientStorageSize = TRANSIENT_STORAGE_SIZE;
    Platform->Input.TransientStorage = (u8*)calloc(1, Platform->Input.TransientStorageSize);
    
    Platform->Input.Text[0] = '\0';
    GlobalTextPos = 0;
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
    Platform->Interface.GetTimeMs = LinuxGetTimeMs;
    Platform->Interface.GetOpenGLProcAddress = LinuxGetOpenGLProcAddress;
    Platform->Interface.LoadEntireFile = LinuxLoadEntireFile;
    Platform->Interface.FreeEntireFile = LinuxFreeEntireFile;
    Platform->Interface.Log = LinuxLog;
    Platform->Interface.SetClipboardText = LinuxSetClipboardText;
    Platform->Interface.GetClipboardText = LinuxGetClipboardText;
    Platform->Interface.WorkQueueAddEntry = LinuxWorkQueueAddEntry;
    Platform->Interface.WorkQueueCompleteAllWork = LinuxWorkQueueCompleteAllWork;
  }
}

internal void PlatformDestroy(platform_state* Platform)
{
  free(Platform->Input.PermanentStorage);
  free(Platform->Input.TransientStorage);
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[]) {
  i32 ExitCode = 0;
  game_library GameLibrary = {};
  
  GameLibraryOpen(&GameLibrary);
  PlatformInit(&GlobalPlatform);
  
  // NOTE: Need to invoke this before making any calls to X windows in order to
  // ensure that it is configured to work properly with multithreading. This
  // lets us perform multithreaded OpenGL rendering to our window.
  XInitThreads();
  
  ///////////////////////////////////////////////////////////////////////////////
  
  GLint VisualAttribs[] = {
    GLX_X_RENDERABLE, True,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 24,
    GLX_STENCIL_SIZE, 8,
    GLX_DOUBLEBUFFER, True,
    // TODO: Support MSAA 2x, 4x, and 8x
    // GLX_SAMPLE_BUFFERS, 1
    // GLX_SAMPLES, 8
    None
  };
  
  // Open connection to the default XWindows service
  printf("X11:\n");
  GlobalDisplay = XOpenDisplay(NULL);
  if (GlobalDisplay != NULL)
  {
    printf("\tDisplay: %s (%d)\n", XDisplayString(GlobalDisplay), ScreenCount(GlobalDisplay));
    printf("\tVendor:  %s\n", XServerVendor(GlobalDisplay));
    printf("\tRelease: %d\n", XVendorRelease(GlobalDisplay));
    
    X11LoadAtoms(GlobalDisplay);
    
    int GLXMajor, GLXMinor;
    
    // FBConfigs were added in GLX version 1.3.
    if ( !glXQueryVersion( GlobalDisplay, &GLXMajor, &GLXMinor ) || 
        ( ( GLXMajor == 1 ) && ( GLXMinor < 3 ) ) || ( GLXMajor < 1 ) )
    {
      fprintf(stderr, "Fatal error: Invalid GLX version (require at least GLX v1.3)");
      exit(1);
    }
    
    i32 FBCount;
    GLXFBConfig *FBConfigs = glXChooseFBConfig(GlobalDisplay, DefaultScreen(GlobalDisplay), VisualAttribs, &FBCount);
    if (!FBCount)
    {
      fprintf(stderr, "Fatal error: Failed to retrieve framebuffer config\n");
      exit(1);
    }
    
    // Pick the FB config/visual with the most samples per pixel
    int BestFBCIndex = -1, WorstFBCIndex = -1, BestNumSamp = -1, WorstNumSamp = 999;
    
    foreach(I, FBCount)
    {
      XVisualInfo *VI = glXGetVisualFromFBConfig(GlobalDisplay, FBConfigs[I]);
      if (VI)
      {
        int SampBuf, Samples;
        glXGetFBConfigAttrib(GlobalDisplay, FBConfigs[I], GLX_SAMPLE_BUFFERS, &SampBuf);
        glXGetFBConfigAttrib(GlobalDisplay, FBConfigs[I], GLX_SAMPLES, &Samples);
        
        if (BestFBCIndex < 0 || (SampBuf && Samples > BestNumSamp))
        {
          BestFBCIndex = I;
          BestNumSamp = Samples;
        }
        if (WorstFBCIndex < 0 || !SampBuf || Samples < WorstNumSamp)
        {
          WorstFBCIndex = I;
          WorstNumSamp = Samples;
        }
      }
      
      XFree(VI);
    }
    
    GLXFBConfig BestFBC = FBConfigs[BestFBCIndex];
    
    // Be sure to free the FBConfig list allocated by glXChooseFBConfig()
    XFree(FBConfigs);
    
    // Get a visual
    XVisualInfo *GLVisual = glXGetVisualFromFBConfig(GlobalDisplay, BestFBC);
    
    // Attempt to provision visuals for OpenGL
    Window Root = DefaultRootWindow(GlobalDisplay);
    if (GLVisual != NULL)
    {
      // Create input manager and input context so we can translate text to
      // UTF-8 strings.
      XSetLocaleModifiers("");
      GlobalXIM = XOpenIM(GlobalDisplay, 0, 0, 0);
      if (!GlobalXIM) {
        XSetLocaleModifiers("@im=none");
        GlobalXIM = XOpenIM(GlobalDisplay, 0, 0, 0);
      }
      
      GlobalXIC = XCreateIC(GlobalXIM, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                            XNClientWindow, GlobalWindow,
                            XNFocusWindow, GlobalWindow,
                            NULL);
      XSetICFocus(GlobalXIC);
      
      // Create color map
      Colormap GLColorMap = XCreateColormap(GlobalDisplay,
                                            RootWindow(GlobalDisplay, GLVisual->screen),
                                            GLVisual->visual,
                                            AllocNone);
      
      XSetWindowAttributes WindowAttribs;
      WindowAttribs.colormap = GLColorMap;
      WindowAttribs.background_pixmap = None;
      WindowAttribs.event_mask = (ExposureMask | KeyPressMask | KeyReleaseMask |
                                  PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                                  VisibilityChangeMask | EnterWindowMask | LeaveWindowMask |
                                  PropertyChangeMask | StructureNotifyMask |
                                  KeymapStateMask | FocusChangeMask | PropertyChangeMask);
      
      // Create a window on the default screen
      i32 DefaultScreen = XDefaultScreen(GlobalDisplay);
      GlobalWindow = XCreateWindow(GlobalDisplay,
                                   Root,
                                   0, 0, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, /* x, y, w, h */
                                   0, /* border width */
                                   GLVisual->depth,
                                   InputOutput, /*class */
                                   GLVisual->visual,
                                   CWBackPixmap | CWColormap | CWEventMask,
                                   &WindowAttribs);

      X11ToggleAllowResizing(false);
      
      XMapRaised(GlobalDisplay, GlobalWindow);
      XSetWMProtocols(GlobalDisplay, GlobalWindow, &WMDeleteWindowAtom, 1);
      XSync(GlobalDisplay, False);
      
      // Load extensions
      const char* GLXExtensionList = glXQueryExtensionsString(GlobalDisplay, DefaultScreen);
      if (ExtensionInList(GLXExtensionList, "GLX_EXT_swap_control"))
      {
        printf("info: Found GLX_EXT_swap_control\n");
        GLXLoadRequiredExtension(glXSwapIntervalEXT, SWAPINTERVALEXT, GLX_EXT_swap_control);
      }
      else if (ExtensionInList(GLXExtensionList, "GLX_MESA_swap_control"))
      {
        printf("info: Found GLX_MESA_swap_control\n");
        GLXLoadRequiredExtension(glXSwapIntervalMESA, SWAPINTERVALMESA, GLX_MESA_swap_control);
      }
      else if (ExtensionInList(GLXExtensionList, "GLX_SGI_swap_control"))
      {
        // GLX_SGI_swap_control does not support setting VSync.
        fprintf(stderr, "warning: No VSync, only GLX_SGI_swap_control available.\n");
      }

      GLXLoadRequiredExtension(glXCreateContextAttribsARB, CREATECONTEXTATTRIBSARB, GLX_ARB_create_context);
      
      // Create OpenGL context
      int ContextAttribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        None
      };
      GLXContext GLCtx = glXCreateContextAttribsARB(GlobalDisplay, BestFBC, 0, True, NULL /*ContextAttribs*/);
      if (GLCtx != NULL)
      {
        if (!glXMakeContextCurrent(GlobalDisplay, GlobalWindow, GlobalWindow, GLCtx))
        {
          fprintf(stderr, "glXMakeContextCurrent failed for window\n");
          exit(1);
        }
        
        X11SetWindowTitle(APP_TITLE, GlobalDisplay, GlobalWindow);
        X11SetWindowIconPNG("icon.png", GlobalDisplay, GlobalWindow);
        
        // Set initial VSync
        LinuxSetVSync(true);
        XSync(GlobalDisplay, False);
        
        // Print basic OpenGL driver info
        printf("OpenGL:\n");
        printf("\tVendor:   %s\n", glGetString(GL_VENDOR));
        printf("\tRenderer: %s\n", glGetString(GL_RENDERER));
        printf("\tVersion:  %s\n", glGetString(GL_VERSION));
        printf("\tGLSL:     %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
        
        // Audio initialization
        linux_audio Audio = {};
        b32 Result = LinuxAudioCreate(&Audio, 2 /* ms latency */, 48000 /* samples / sec */);
        if (Result)
        {
          ///////////////////////////////////////////////////////////////////////////////
          // Audio system initialization

          // NOTE: We want to be able to write up to the entire circular audio
          // buffer in size each frame, so allocate our game output sample
          // buffer to be equal in size to the circular buffer.
          LinuxAudioStart(&Audio);
          GlobalPlatform.Shared.AudioBuffer.Samples = (i16 *)calloc(1, sizeof(i16) * Audio.CircularBuffer.SampleCount);
          GlobalPlatform.Shared.AudioBuffer.SamplesPerSecond = Audio.SamplesPerSecond;
          GlobalPlatform.Shared.AudioBuffer.FrameCount = 0;
          
          ////////////////////////////////////////////////////////////////////////////
          // Spawn worker threads
          
          // Create a sempahore (passing 0 for the second argument means it cannot be
          // shared between processes).
          sem_t WorkerSemaphore;
          sem_init(&WorkerSemaphore, 0, WORKER_THREAD_COUNT);
          
          // Spawn worker threads
          thread_atomic_int_t WorkerThreadExitFlag;
          thread_atomic_int_store(&WorkerThreadExitFlag, 0);

          work_queue Queue = {};
          Queue.Semaphore = &WorkerSemaphore;

          thread_ptr_t WorkerThread[WORKER_THREAD_COUNT];
          worker_thread_info WorkerThreadInfo[WORKER_THREAD_COUNT];
          foreach(I, WORKER_THREAD_COUNT) {
            worker_thread_info *Info = WorkerThreadInfo + I;
            Info->ThreadIndex = I;
            Info->ExitFlag = &WorkerThreadExitFlag;
            Info->Queue = &Queue;
            Info->OpenGLContext = glXCreateContextAttribsARB(GlobalDisplay, BestFBC, GLCtx, True, NULL /*ContextAttribs*/);
            WorkerThread[I] = thread_create(WorkerThreadLoop, Info, THREAD_STACK_SIZE_DEFAULT);
          }

          // Set platform work queue
          GlobalPlatform.Input.WorkQueue = &Queue;
          
          u64 DeltaTimeStart = LinuxGetTimeMicros();
          while (GlobalPlatform.Shared.IsRunning) {
            GameLibrary.OnFrameStart(&GlobalPlatform);
            
            // Process input
            while (X11PumpEvents(&GlobalPlatform)) { }
            
            // Store previous
            b32 OldVSync = GlobalPlatform.Shared.VSync;
            b32 OldFullScreen = GlobalPlatform.Shared.FullScreen;

            // Get number of audio frames to write this video frame
            GlobalPlatform.Shared.AudioBuffer.FrameCount = LinuxAudioFramesToWrite(&Audio);

            {
              Window Root, Child;
              i32 RootX, RootY, XPos, YPos;
              u32 ButtonMasks;
              XQueryPointer(GlobalDisplay,
                            GlobalWindow,
                            &Root, &Child,
                            &RootX, &RootY,
                            &XPos, &YPos,
                            &ButtonMasks);
              GlobalPlatform.Input.Mouse.Pos = V2I(XPos, GlobalPlatform.Input.RenderDim.Height - YPos);
            }
            
            GameLibraryOpen(&GameLibrary);
            u64 EndTime = LinuxGetTimeMicros();
            u64 DeltaTimeMicros = EndTime - DeltaTimeStart;
            GameLibrary.Update(&GlobalPlatform, DeltaTimeMicros);
            DeltaTimeStart = EndTime;

            // Queue up game audio for audio thread
            LinuxAudioFill(&Audio, GlobalPlatform.Shared.AudioBuffer.Samples, GlobalPlatform.Shared.AudioBuffer.FrameCount);

            // Render
            glXSwapBuffers(GlobalDisplay, GlobalWindow);

            // Reset single-frame platform state
            PlatformEndFrameReset(&GlobalPlatform);
            
            // Update any state requested by the game
            if (OldVSync != GlobalPlatform.Shared.VSync)
            {
              LinuxSetVSync(GlobalPlatform.Shared.VSync);
              XSync(GlobalDisplay, False);
            }
            
            if (OldFullScreen != GlobalPlatform.Shared.FullScreen)
            {
              // NOTE: @Requirement: Arbitrary window resizing is not allowed.
              //
              // To allow for fullscreening we need to allow window resizing
              // temporarily here. But in general we discourage letting the
              // user / windowing system arbitrarily resize our window and we
              // don't guarantee fidelity for arbitrary window sizes.
              if (GlobalPlatform.Shared.FullScreen) {
                X11ToggleAllowResizing(true);
              } else {
                X11ToggleAllowResizing(false);
              }

              Atom WMStateAtom = XInternAtom(GlobalDisplay, "_NET_WM_STATE", False);
              Atom FullScreenAtom = XInternAtom(GlobalDisplay, "_NET_WM_STATE_FULLSCREEN", False);
              i32 Mask = SubstructureNotifyMask | SubstructureRedirectMask;
              XEvent Event = {};
              Event.type = ClientMessage;
              Event.xclient.serial = 0;
              Event.xclient.send_event = True;
              Event.xclient.window = GlobalWindow;
              Event.xclient.message_type = WMStateAtom;
              Event.xclient.format = 32;
              Event.xclient.data.l[0] = (GlobalPlatform.Shared.FullScreen) ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
              Event.xclient.data.l[1] = FullScreenAtom;
              Event.xclient.data.l[2] = 0;
              
              XSendEvent(GlobalDisplay, DefaultRootWindow(GlobalDisplay), False, Mask, &Event);
            }

            GameLibrary.OnFrameEnd(&GlobalPlatform);
          }
          
          // Destroy worker threads
          thread_atomic_int_store(&WorkerThreadExitFlag, 1);
          foreach(I, WORKER_THREAD_COUNT) {
            int ReturnValue = thread_join(WorkerThread[I]);
            printf("Worker Thread %d: Exit Code %d\n", I, ReturnValue);
            thread_destroy(WorkerThread[I]);
          }
          
          // Destroy our worker sempahore
          sem_destroy(&WorkerSemaphore);

          // Shutdown game
          GameLibrary.Shutdown(&GlobalPlatform);

          // Destroy audio
          LinuxAudioDestroy(&Audio);
          free(GlobalPlatform.Shared.AudioBuffer.Samples);
        }
        else
        {
          fprintf(stderr, "error: failed to initialize ALSA: %s\n", snd_strerror(Result));
          ExitCode = 1;
        }
        
        glXMakeCurrent(GlobalDisplay, None, NULL);
        glXDestroyContext(GlobalDisplay, GLCtx);
        XSync(GlobalDisplay, False);
      }
      else
      {
        fprintf(stderr, "GLX error: Failed to create OpenGL context. \n");
        ExitCode = 1;
      }
      
      XUnmapWindow(GlobalDisplay, GlobalWindow);
      XFreeColormap(GlobalDisplay, GLColorMap);
      XDestroyWindow(GlobalDisplay, GlobalWindow);
      XFree(GLVisual);
    }
    else
    {
      fprintf(stderr, "GLX error: Failed to find suitable visual.\n");
      ExitCode = 1;
    }
    
    XCloseDisplay(GlobalDisplay);
  }
  else
  {
    fprintf(stderr, "Fatal error: Failed to open connection to default XWindows server.\n");
    ExitCode = 1;
  }
  
  GameLibraryClose(&GameLibrary);
  PlatformDestroy(&GlobalPlatform);
  
  return(ExitCode);
}
