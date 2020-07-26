#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#define DEBUG_CONSOLE_MAX_LINES 128
#define DEBUG_CONSOLE_MAX_LINE_LENGTH 128

typedef enum console_mode {
  CONSOLE_MODE_unloaded,
  CONSOLE_MODE_loading,
  CONSOLE_MODE_loaded,
  CONSOLE_MODE_unloading
} console_mode;

typedef struct scrollbar {
  f32 Width;
  f32 Height;
} scrollbar;

typedef struct console {
  font *Font;
  console_mode Mode;
  f32 TimePassed;
  
  u32 CursorPos;
  
  u32 SelectionStart;
  u32 SelectionEnd;

  b32 ThumbFocus;
  f32 YScrollOffset;
  v2 Mouse;
  v2 MouseDelta;

  u32 LogLineCount;
  char Log[DEBUG_CONSOLE_MAX_LINES][DEBUG_CONSOLE_MAX_LINE_LENGTH];
  char Input[DEBUG_CONSOLE_MAX_LINE_LENGTH];
} console;

internal void ConsoleCreate(console *Console, font *Font);
// Is the console currently being display and actively processing input?
internal b32 ConsoleIsActive(console *Console);
internal void ConsoleLog(console *Console, const char *Text);
internal void ConsoleUpdate(console *Console, platform_state *Platform, f32 DeltaTimeSecs);

internal void ConsoleInputMouse(console *Console, v2 MousePos);

#endif //DEBUG_CONSOLE_H
