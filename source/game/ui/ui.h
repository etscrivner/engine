#ifndef UI_H
#define UI_H

#include "common/language_layer.h"
#include "fonts.h"
#include "textures.h"
#include "renderer.h"

typedef struct app_context app_context;

#define UI_COMMAND_BUFFER_SIZE Megabytes(32)
#define UI_CONTAINER_STACK_SIZE 32
#define UI_ID_STACK_SIZE 32
#define UI_CONTAINER_POOL_SIZE 32

typedef u32 ui_id;

enum {
  UI_ICON_close,
  UI_ICON_MAX
};

enum {
  UI_COMMAND_rect,
  UI_COMMAND_text,
  UI_COMMAND_icon,
  UI_COMMAND_MAX
};

typedef struct { i32 Type; u32 SizeBytes; } ui_base_command;
typedef struct { ui_base_command Base; v4 Rect; v4 Color; } ui_rect_command;
typedef struct { ui_base_command Base; v2 Pos; v4 Color; char Str[1]; } ui_text_command;
typedef struct { ui_base_command Base; v4 Rect; i32 ID; v4 Color; } ui_icon_command;

typedef union {
  i32 Type;
  ui_base_command Base;
  ui_rect_command Rect;
  ui_text_command Text;
  ui_icon_command Icon;
} ui_command;

// Pools solve the problem of an immediate-mode UI never truly knowing which
// windows previously allocated are free for new use. To solve this problem, we
// retain a arrays mapping ID values to the last-used frame for that
// ID. Whenever we run out of available items in a pool, we reclaim the least
// recently used pool item. Pool sizes should be tuned large enough that this
// is unlikely in practice.
typedef struct ui_pool_item {
  ui_id ID;
  u64 LastUsed;
} ui_pool_item;

// Containers store the state information for windows and panels. Widgets
// within a window or panel don't need any state as their current state can be
// derived each frame entirely from their container's state and layout
// information.
typedef struct ui_container {
  v4 Rect;
  v4 Body;
  b32 IsOpen;
  u64 ZIndex;
} ui_container;

typedef struct ui_context {
  font *Font;
  u64 Frame;
  u64 LastZIndex;

  b32 KeyboardInputConsumed;
  b32 MouseInputConsumed;
  b32 TextInputConsumed;

  Stack(u8, UI_COMMAND_BUFFER_SIZE) CommandList;
  Stack(ui_id, UI_ID_STACK_SIZE) IDStack;
  Stack(ui_container*, UI_CONTAINER_STACK_SIZE) ContainerStack;

  ui_pool_item ContainerPool[UI_CONTAINER_POOL_SIZE];
  ui_container Containers[UI_CONTAINER_POOL_SIZE];
} ui_context;

internal void UICreate(ui_context *UI, font *Font);
internal void UIBegin(ui_context *UI);
internal void UIEnd(ui_context *UI);

internal i32 UIBeginWindow(ui_context *UI, char *Title, v4 Rect);
internal void UIEndWindow(ui_context *UI);

internal void UIBringToFront(ui_context *UI, ui_container *Container);

internal void UIRender(ui_context *UI, renderer *Renderer, texture_catalog *TextureCatalog, platform_state *Platform);

// 
// Custom UI system below
//

typedef struct button_style {
  v4 BackgroundColor;
  v4 HoverBackgroundColor;
  v4 TextColor;
  font *Font;
} button_style;

internal b32 DrawRectButton(renderer *Renderer, app_context Ctx, v4 Rect, button_style Style);
internal b32 DrawSpriteButton(renderer *Renderer, app_context Ctx, sprite Sprite, v4 Rect, button_style Style);
internal b32 DrawButton(renderer *Renderer, app_context Ctx, char *Title, v4 Rect, button_style Style);
internal b32 DrawCheckbox(renderer *Renderer, app_context Ctx, texture_catalog *TextureCatalog, char *Title, v4 Rect, button_style Style, b32 *Value);

typedef struct widget_id {
  i32 ParentID;
  i32 ID;
  i32 Index;
} widget_id;

inline bool operator == (widget_id Left, widget_id Right)
{
  return (Left.ParentID == Right.ParentID && Left.ID == Right.ID && Left.Index == Right.Index);
}

widget_id WIDGET_ID_none = {-1, -1, -1};

typedef struct ui_style {
  f32 TitleBarHeight;
  f32 ButtonHeight;
  v2 MinWindowSize;
  v4 WindowColor;
  v4 TitleBarColor;
  button_style CloseButton;
  button_style Button;
} ui_style;

typedef struct ui_window {
  b32 IsOpen;
  v2i Offset; // Position offset of window (moving)
  v2i SizeOffset; // Size offset of window (resizing)
} ui_window;

typedef struct ui_layout {
  v4 Next;
} ui_layout;

typedef struct ui_state {
  widget_id Hot;
  widget_id Active;

  v2i MousePos;
  v2i LastMousePos;
  v2i MouseDelta;

  widget_id CurrentParent;
  v4 ParentOffset;
  v4 ActiveClip;
  v4 LayoutNext;

  font *Font;
  texture_catalog *TextureCatalog;
  renderer *Renderer;
  ui_style Style;

  // Next frames possible hot and active widgets
  widget_id NextHot;
  widget_id NextActive;
} ui_state;

#endif // UI_H
