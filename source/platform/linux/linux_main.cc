#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <dlfcn.h>
#include <time.h>

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

// ALSA (Audio)
#define ALSA_PCM_NEW_HW_PARAMS_API // Use the new ALSA API
#include <alsa/asoundlib.h>

#include "game.h"

#define HandleButtonPress(ButtonArray, Item, IsDown) do {       \
    (ButtonArray)[(Item)].Pressed = (IsDown);                   \
    (ButtonArray)[(Item)].Down = (IsDown);                      \
} while(0)

#define GAME_LIBRARY "./libgame.so"
#define GAME_LIBRARY_LOCKFILE "./build.lock"

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

#define GLXProc(Type, Name) PFNGLX##Type##PROC glX##Name;

// glX procs we care about
GLXProc(SWAPINTERVALEXT, SwapIntervalEXT)

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
static b32 GlobalSelectionWaiting = false;
static platform_state GlobalPlatform = {};

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

internal b32 X11GetClipboard(Display *CurrentDisplay, Window CurrentWindow)
{
  b32 Result = false;

  // Check if there is an owner for the clipboard atom
  Window Owner = XGetSelectionOwner(CurrentDisplay, ClipboardAtom);
  if (Owner != None)
  {
    XConvertSelection(CurrentDisplay, ClipboardAtom, UTF8StringAtom, PlatformTargetPropertyAtom, CurrentWindow, CurrentTime);
    XFlush(CurrentDisplay);
  }
  
  return(Result);
}

internal b32 X11PumpEvents(platform_state *Platform)
{
  local_persist XEvent Event;
  local_persist XWindowAttributes EventWindowAttrs;
  b32 Result = false;

  if (XPending(GlobalDisplay))
  {
    Result = true;
    XNextEvent(GlobalDisplay, &Event);

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
      Platform->Input.RenderDim = V2(EventWindowAttrs.width, EventWindowAttrs.height);
      glViewport(0, 0, EventWindowAttrs.width, EventWindowAttrs.height);

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
                  
      Platform->Input.Mouse.Pos = V2(XPos, YPos);
      Platform->Input.Mouse.Pos01 = Clamp01(Platform->Input.Mouse.Pos / Platform->Input.RenderDim);
    }
    break;
    case ResizeRequest:
    {
      XGetWindowAttributes(GlobalDisplay, GlobalWindow, &EventWindowAttrs);
      Platform->Input.RenderDim = V2(EventWindowAttrs.width, EventWindowAttrs.height);
      glViewport(0, 0, EventWindowAttrs.width, EventWindowAttrs.height);
    }
    break;
    case MotionNotify:
    {
      Platform->Input.Mouse.Pos = V2(Event.xmotion.x, Event.xmotion.y);
      Platform->Input.Mouse.Pos01 = Platform->Input.Mouse.Pos / Platform->Input.RenderDim;
    }
    break;
    case ButtonPress:
    case ButtonRelease:
    {
      b32 IsDown = (Event.type == ButtonPress);
                  
      Platform->Input.Mouse.Pos = V2(Event.xmotion.x, Event.xmotion.y);
      Platform->Input.Mouse.Pos01 = Platform->Input.Mouse.Pos / Platform->Input.RenderDim;

      if (Event.xbutton.button == Button1)
      {
        HandleButtonPress(Platform->Input.Mouse.Button, MOUSE_BUTTON_left, IsDown);
      }
      if (Event.xbutton.button == Button2)
      {
        HandleButtonPress(Platform->Input.Mouse.Button, MOUSE_BUTTON_middle, IsDown);
      }
      if (Event.xbutton.button == Button3)
      {
        HandleButtonPress(Platform->Input.Mouse.Button, MOUSE_BUTTON_right, IsDown);
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

      if (Sym == XK_Escape) // Escape
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_esc, IsDown);
      }
      else if (Sym == XK_BackSpace) // Backspace
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_backspace, IsDown);
      }
      else if (Sym == XK_Delete) // Delete
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_delete, IsDown);
      }
      else if (Sym == XK_Tab) // Tab
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_tab, IsDown);
      }
      else if (Sym == XK_Return) // Enter/Return
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_enter, IsDown);
      }
      else if (Sym == XK_Control_L || Sym == XK_Control_R) // Control
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_ctrl, IsDown);
      }
      else if (Sym == XK_Meta_L || Sym == XK_Meta_R || Sym == XK_Alt_L || Sym == XK_Alt_R) // Alt/Meta
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_alt, IsDown);
      }
      else if (Sym == XK_Shift_L || Sym == XK_Shift_R) // Shift
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_shift, IsDown);
      }
      else if (Sym >= XK_Left && Sym <= XK_Down) // Arrow keys (Left, Up, Right, Down)
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_left + (Sym - XK_Left), IsDown);
      }
      else if (AsciiKeyCode >= 0x20 && AsciiKeyCode <= 0x40) // Space - @ (Includes 0-9)
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_space + (AsciiKeyCode - 0x20), IsDown);
      }
      else if (AsciiKeyCode >= 0x41 && AsciiKeyCode <= 0x60) // a-z to `
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_a + (AsciiKeyCode - 0x41), IsDown);
      }
      else if (AsciiKeyCode >= 0xBE && AsciiKeyCode <= 0xC9) // F1 - F12
      {
        HandleButtonPress(Platform->Input.Keyboard.Key, KEY_f1 + (AsciiKeyCode - 0xBE), IsDown);
      }
    }
    break;
    case EnterNotify:
      printf("EnterNotify\n");
      break;
    case LeaveNotify:
      printf("LeaveNotify\n");
      break;
    case FocusIn:
      printf("FocusIn\n");
      break;
    case FocusOut:
      printf("FocusOut\n");
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
  }

  return(Result);
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

internal char* LinuxGetClipboardText(scoped_arena* ScopedArena) {
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
  }
  
  // Shared state
  {
    Platform->Shared.IsRunning = true;
    Platform->Shared.TargetFPS = 60.0f;
    Platform->Shared.VSync = false;
    Platform->Shared.FullScreen = false;
  }
  
  // Interfaces
  {
    Platform->Interface.GetTimeSecs = LinuxGetTimeSecs;
    Platform->Interface.GetOpenGLProcAddress = LinuxGetOpenGLProcAddress;
    Platform->Interface.LoadEntireFile = LinuxLoadEntireFile;
    Platform->Interface.FreeEntireFile = LinuxFreeEntireFile;
    Platform->Interface.Log = LinuxLog;
    Platform->Interface.SetClipboardText = LinuxSetClipboardText;
    Platform->Interface.GetClipboardText = LinuxGetClipboardText;
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
  
  ///////////////////////////////////////////////////////////////////////////////
  
  // Desired OpenGL video mode
  GLint DesiredVideoMode[] = {
    GLX_RGBA,
    GLX_DEPTH_SIZE, 24,
    GLX_DOUBLEBUFFER,
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
    
    // Attempt to provision visuals for OpenGL
    Window Root = DefaultRootWindow(GlobalDisplay);
    XVisualInfo *GLVisual = glXChooseVisual(GlobalDisplay, 0, DesiredVideoMode);
    if (GLVisual != NULL)
    {
      // Create color map
      Colormap GLColorMap = XCreateColormap(GlobalDisplay, Root, GLVisual->visual, AllocNone);
      
      XSetWindowAttributes WindowAttribs;
      WindowAttribs.colormap = GLColorMap;
      WindowAttribs.background_pixmap = None;
      WindowAttribs.event_mask = (
        ExposureMask | KeyPressMask | KeyReleaseMask |
        PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
        VisibilityChangeMask | EnterWindowMask | LeaveWindowMask |
        PropertyChangeMask | StructureNotifyMask |
        KeymapStateMask | FocusChangeMask | PropertyChangeMask
      );
      
      // Create a window on the default screen
      i32 DefaultScreen = XDefaultScreen(GlobalDisplay);
      GlobalWindow = XCreateWindow(
        GlobalDisplay,
        Root,
        0, 0, 800, 600, /* x, y, w, h */
        0, /* border width */
        GLVisual->depth,
        InputOutput, /*class */
        GLVisual->visual,
        CWBackPixmap | CWColormap | CWEventMask,
        &WindowAttribs
      );
      
      XMapRaised(GlobalDisplay, GlobalWindow);
      XSetWMProtocols(GlobalDisplay, GlobalWindow, &WMDeleteWindowAtom, 1);
      XSync(GlobalDisplay, False);

      X11SetWindowTitle(APP_TITLE, GlobalDisplay, GlobalWindow);
      X11SetWindowIconPNG("icon.png", GlobalDisplay, GlobalWindow);
      
      // Create OpenGL context
      GLXContext GLCtx = glXCreateContext(GlobalDisplay, GLVisual, NULL, GL_TRUE);
      if (GLCtx != NULL)
      {
        glXMakeCurrent(GlobalDisplay, GlobalWindow, GLCtx);
        
        // Load extensions
        const char* GLXExtensionList = glXQueryExtensionsString(GlobalDisplay, DefaultScreen);
        GLXLoadRequiredExtension(glXSwapIntervalEXT, SWAPINTERVALEXT, GLX_EXT_swap_control);
        
        // Set initial VSync
        glXSwapIntervalEXT(GlobalDisplay, GlobalWindow, 1);
        
        // Print basic OpenGL driver info
        printf("OpenGL:\n");
        printf("\tVendor:   %s\n", glGetString(GL_VENDOR));
        printf("\tRenderer: %s\n", glGetString(GL_RENDERER));
        printf("\tVersion:  %s\n", glGetString(GL_VERSION));
        printf("\tGLSL:     %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
        
        // Audio initialization
        printf("ALSA:\n");
        printf("\tVersion: %s\n", SND_LIB_VERSION_STR);
        
        snd_pcm_t *AudioHandle;
        i32 Result = snd_pcm_open(&AudioHandle, "default", SND_PCM_STREAM_PLAYBACK, 0);
        
        if (Result >= 0)
        {
          // Configure audio integration
          snd_pcm_hw_params_t *HWParams;
          snd_pcm_hw_params_alloca(&HWParams);
          
          u32 SamplingRate = 44100;
          i32 SubunitDirection = 0;
          i32 Periods = 2;
          snd_pcm_uframes_t PeriodSize = 8192;
          snd_pcm_hw_params_any(AudioHandle, HWParams);
          snd_pcm_hw_params_set_access(AudioHandle, HWParams, SND_PCM_ACCESS_RW_INTERLEAVED);
          snd_pcm_hw_params_set_format(AudioHandle, HWParams, SND_PCM_FORMAT_S16_LE);
          snd_pcm_hw_params_set_channels(AudioHandle, HWParams, 2);
          snd_pcm_hw_params_set_rate_near(AudioHandle, HWParams, &SamplingRate, &SubunitDirection);
          snd_pcm_hw_params_set_periods(AudioHandle, HWParams, Periods, 0);
          snd_pcm_hw_params_set_buffer_size(AudioHandle, HWParams, (Periods * PeriodSize) >> 2);
          
          Result = snd_pcm_hw_params(AudioHandle, HWParams);
          if (Result < 0) {
            fprintf(stderr, "error: unable to set audio hw parameters: %s\n", snd_strerror(Result));
            exit(1);
          }

          GlobalPlatform.Shared.AudioBuffer.Size = Periods * PeriodSize * sizeof(i16) * 1;
          GlobalPlatform.Shared.AudioBuffer.SizeSamples = PeriodSize >> 2;
          GlobalPlatform.Shared.AudioBuffer.Buffer = (u8*)calloc(1, GlobalPlatform.Shared.AudioBuffer.Size);
          
          // Pause audio at the start
          snd_pcm_drop(AudioHandle);
          
          b32 AudioStarted = false;
          f32 DeltaTimeStart = LinuxGetTimeSecs();
          while (GlobalPlatform.Shared.IsRunning) {
            GameLibrary.OnFrameStart(&GlobalPlatform);
            
            // Process input
            while (X11PumpEvents(&GlobalPlatform)) { }

            // Store previous
            b32 OldVSync = GlobalPlatform.Shared.VSync;
            b32 OldFullScreen = GlobalPlatform.Shared.FullScreen;
            
            if (!AudioStarted) {
              snd_pcm_prepare(AudioHandle);
              AudioStarted = true;
            }

            GameLibraryOpen(&GameLibrary);
            f32 DeltaTimeSecs = LinuxGetTimeSecs() - DeltaTimeStart;
            GameLibrary.Update(&GlobalPlatform, DeltaTimeSecs);
            DeltaTimeStart = LinuxGetTimeSecs();

            // Audio
            Result = snd_pcm_writei(AudioHandle, GlobalPlatform.Shared.AudioBuffer.Buffer, GlobalPlatform.Shared.AudioBuffer.SizeSamples);
            
            if (Result == -EPIPE) {
              fprintf(stderr, "error: audio underrun: %s\n", snd_strerror(Result));
              snd_pcm_prepare(AudioHandle);
            } else if (Result < 0) {
              fprintf(stderr, "error: audio writei failed: %s\n", snd_strerror(Result));
            } else if (Result != (i32)PeriodSize >> 2) {
              fprintf(stderr, "short write, wrote %d frames %lu expected\n", Result, PeriodSize);
            }
            
            // Render
            glXSwapBuffers(GlobalDisplay, GlobalWindow);
            
            // Reset single-frame platform state
            PlatformEndFrameReset(&GlobalPlatform);
            
            // Update any state requested by the game
            if (OldVSync != GlobalPlatform.Shared.VSync)
            {
              glXSwapIntervalEXT(GlobalDisplay, GlobalWindow, GlobalPlatform.Shared.VSync ? 1 : 0);
            }
            
            if (OldFullScreen != GlobalPlatform.Shared.FullScreen)
            {
              Atom WMStateAtom = XInternAtom(GlobalDisplay, "_NET_WM_STATE", False);
              Atom FullScreenAtom = XInternAtom(GlobalDisplay, "_NET_WM_STATE_FULLSCREEN", False);
              XEvent Event;
              memset(&Event, 0, sizeof(Event));
              Event.type = ClientMessage;
              Event.xclient.window = GlobalWindow;
              Event.xclient.message_type = WMStateAtom;
              Event.xclient.format = 32;
              if (GlobalPlatform.Shared.FullScreen)
              {
                Event.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
              }
              else
              {
                Event.xclient.data.l[0] = 0; // _NET_WM_STATE_REMOVE
              }
              Event.xclient.data.l[1] = FullScreenAtom;
              Event.xclient.data.l[2] = 0;
              
              XSendEvent(GlobalDisplay, DefaultRootWindow(GlobalDisplay), False, SubstructureNotifyMask, &Event);
            }

            GameLibrary.OnFrameEnd(&GlobalPlatform);
          }
          
          GameLibrary.Shutdown(&GlobalPlatform);
          
          snd_pcm_drop(AudioHandle);
          snd_pcm_drain(AudioHandle);
          snd_pcm_close(AudioHandle);
          free(GlobalPlatform.Shared.AudioBuffer.Buffer);
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
    fprintf(stderr, "Failed to open connection to default XWindows server.\n");
    ExitCode = 1;
  }
  
  GameLibraryClose(&GameLibrary);
  PlatformDestroy(&GlobalPlatform);
  
  return(ExitCode);
}
