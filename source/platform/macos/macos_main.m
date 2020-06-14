#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h> // For: Key codes

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <time.h>

#include "game.h"

#define GAME_LIBRARY "./libgame.dylib"

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

@class PlatformAppDelegate;

struct OSXCocoaContext
{
  PlatformAppDelegate *AppDelegate;
  NSWindow *Window;
  NSString *AppName;
  NSString *WorkingDirectory;  
};

@interface PlatformAppDelegate : NSObject<NSApplicationDelegate, NSWindowDelegate>
@end

@implementation PlatformAppDelegate
- (void)applicationDidFinishLaunching:(id)sender
{ }

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
  return YES;
}

- (void)applicationWillTerminate:(NSApplication*)sender
{ }

- (NSSize)windowWillResize:(NSWindow*)window toSize:(NSSize)frameSize
{
  return(frameSize);
}

- (void)windowWillClose:(id)sender
{ }
@end

static NSOpenGLContext* GlobalGLContext;

@interface PlatformView : NSOpenGLView
@end

@implementation PlatformView
- (id)init
{
  self = [super init];
  return(self);
}

- (void)prepareOpenGL
{
  [super prepareOpenGL];
  [[self openGLContext] makeCurrentContext]; 
}

- (void)reshape
{
  [super reshape];
  NSRect Bounds = [self bounds];
  [GlobalGLContext makeCurrentContext];
  [GlobalGLContext update];
  glViewport(0, 0, Bounds.size.width, Bounds.size.height);
}
@end

NSOpenGLView* OSXInitOpenGLView(NSWindow* Window)
{
  NSView* CV = [Window contentView];

  NSOpenGLPixelFormatAttribute GLAttributes[] = {
    NSOpenGLPFAAccelerated,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
    NSOpenGLPFADepthSize, 24,
    NSOpenGLPFAOpenGLProfile,
    NSOpenGLProfileVersion3_2Core,
    0
  };

  NSOpenGLPixelFormat *PixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:GLAttributes];

  GlobalGLContext = [[NSOpenGLContext alloc] initWithFormat:PixelFormat shareContext:NULL];

  PlatformView *GLView = [[PlatformView alloc] init];
  [GLView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [GLView setPixelFormat:PixelFormat];
  [GLView setOpenGLContext:GlobalGLContext];
  [GLView setFrame:[CV bounds]];

  [CV addSubview:GLView];

  [PixelFormat release];

  // Sync
  GLint useVsync = 1;
  [GlobalGLContext setValues:&useVsync forParameter:NSOpenGLContextParameterSwapInterval];
  [GlobalGLContext setView:[Window contentView]];
  [GlobalGLContext makeCurrentContext];

  return(GLView);
}

void OSXCreateSimpleMainMenu(NSString* AppName)
{
  NSMenu *MainMenu = [NSMenu new];
  NSMenuItem *AppMenuItem = [[NSMenuItem alloc] initWithTitle:@"App" action:nil keyEquivalent:@""];
  [MainMenu addItem:AppMenuItem];

  NSMenu* AppMenu = [[NSMenu new] initWithTitle:@"Test"];

  [AppMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"]];
  [AppMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"]];

  [AppMenuItem setSubmenu:AppMenu];
  [NSApp setMainMenu:MainMenu];
}

OSXCocoaContext OSXInitCocoaContext(NSString* AppName, f32 WindowWidth, f32 WindowHeight)
{
  OSXCocoaContext Result = {};

  NSApplication *App = [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#if 0
  NSString *Dir = [[NSFileManager defaultManager] currentDirectoryPath];
  NSFileManager *FileManager = [NSFileManager defaultManager];
#endif
  Result.WorkingDirectory = [NSString stringWithFormat:@"%@/Contents/Resources", [[NSBundle mainBundle] bundlePath]];
#if 0
  if ([FileManager changeCurrentDirectoryPath:Result.WorkingDirectory] == NO)
  {
    assert(0);
  }
#endif
  NSLog(@"working directory: %@", Result.WorkingDirectory);

  Result.AppDelegate = [[PlatformAppDelegate alloc] init];
  [App setDelegate:Result.AppDelegate];

  [NSApp finishLaunching];

  NSRect ScreenRect = [[NSScreen mainScreen] frame];
  NSRect InitialFrame = NSMakeRect(
    (ScreenRect.size.width - WindowWidth) * 0.5,
    (ScreenRect.size.height - WindowHeight) * 0.5,
    WindowWidth,
    WindowHeight
  );

  NSWindow *Window = [[NSWindow alloc] initWithContentRect:InitialFrame styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
  [Window setDelegate:Result.AppDelegate];

  NSView *View = [Window contentView];
  [View setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
  [View setAutoresizesSubviews:YES];

  [Window setMinSize:NSMakeSize(160, 90)];
  [Window setTitle:AppName];
  [Window makeKeyAndOrderFront:nil];

  Result.Window = Window;
  Result.AppName = AppName;

  return(Result);
}

void OSXProcessPendingMessages(platform_state* Platform)
{
  NSEvent *Event = nil;

  do {
    Event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES];
    if ([Event type] == NSEventTypeKeyDown)
    {
      if ([Event keyCode] == kVK_Escape)
      {
        Platform->Shared.IsRunning = false;
      }
    }
    else
    {
      [NSApp sendEvent:Event];
    }
  } while(Event != nil);
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
  game_library GameLibrary = {};
  platform_state Platform = {};
  Platform.Shared.IsRunning = true;

  GameLibraryOpen(&GameLibrary);

  @autoreleasepool
  {
    NSString *AppName = @"Plague 2.0";
    OSXCocoaContext OSXAppContext = OSXInitCocoaContext(
      AppName, 1920, 1080
    );

    OSXCreateSimpleMainMenu(AppName);
    OSXInitOpenGLView(OSXAppContext.Window);

    while (Platform.Shared.IsRunning)
    {
      OSXProcessPendingMessages(&Platform);
      GameLibraryOpen(&GameLibrary);

      [GlobalGLContext makeCurrentContext];
      GameLibrary.Update(&Platform, 0.0f);
      [GlobalGLContext flushBuffer];
    }
  } // @autoreleasepool

  GameLibraryClose(&GameLibrary);
  return(0);
}
