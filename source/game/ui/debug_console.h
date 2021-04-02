#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#define DEBUG_CONSOLE_MAX_LINES 128
#define DEBUG_CONSOLE_MAX_LINE_LENGTH 128

typedef struct app_context app_context;

typedef enum console_mode {
  CONSOLE_MODE_unloaded,
  CONSOLE_MODE_loading,
  CONSOLE_MODE_loaded,
  CONSOLE_MODE_unloading
} console_mode;

enum {
  CONSOLE_COLOR_scrollbar_bg,
  CONSOLE_COLOR_scrollbar_thumb,
  CONSOLE_COLOR_MAX
};

typedef struct console console;
typedef void console_command_fn(console*, app_context, char*);

typedef struct console_command {
  char *Command;
  console_command_fn *Cmd;
} console_command;

typedef struct console_style {
  f32 ThumbPadding;
  v4 Colors[CONSOLE_COLOR_MAX];
} console_style;

typedef struct console {
  font *Font;
  memory_arena *TransientArena;

  b32 KeyboardInputConsumed;
  b32 MouseInputConsumed;
  b32 TextInputConsumed;

  console_mode Mode;
  u64 TimePassedMicros;
  console_style Style;
  
  u32 CursorPos;
  
  u32 SelectionStart;
  u32 SelectionEnd;

  v4 ScrollBarRect;
  v4 ThumbRect;
  f32 TextHeight;
  f32 YOffset; // Y-offset of console when dropping down

  b32 ThumbFocus;
  f32 YScrollOffset; // Offset of scrollable area
  v2i Mouse;
  v2i MouseDelta;
  v2u RenderDim;
  v2u WindowDim;

  u32 LogLineCount;
  char Log[DEBUG_CONSOLE_MAX_LINES][DEBUG_CONSOLE_MAX_LINE_LENGTH];
  char Input[DEBUG_CONSOLE_MAX_LINE_LENGTH];
} console;

internal void ConsoleCreate(console *Console, font *Font, memory_arena *TransientArena);
// Is the console currently being displayed and actively processing input?
internal b32 ConsoleIsActive(console *Console);
internal void ConsoleLog(console *Console, const char *Text);
internal void ConsoleLogf(console *Console, const char *Fmt, ...);
internal void ConsoleUpdate(console *Console, app_context Ctx, u64 DeltaTimeMicros);
internal void ConsoleRender(console *Console, renderer *Renderer);

#endif //DEBUG_CONSOLE_H
