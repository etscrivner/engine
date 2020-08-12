#include "ui.h"
#include "common/language_layer.h"
#include "renderer.h"

internal ui_command* UIPushCommand(ui_context *UI, i32 Type, u32 Size)
{
  ui_command *Cmd = (ui_command*)(UI->CommandList.Items + UI->CommandList.Index);
  Assert(UI->CommandList.Index + Size < UI_COMMAND_BUFFER_SIZE);
  Cmd->Base.Type = Type;
  Cmd->Base.SizeBytes = Size;
  UI->CommandList.Index += Size;
  return(Cmd);
}

internal void UIPushRect(ui_context *UI, v4 Rect, v4 Color)
{
  ui_command *Cmd = UIPushCommand(UI, UI_COMMAND_rect, sizeof(ui_rect_command));
  Cmd->Rect.Rect = Rect;
  Cmd->Rect.Color = Color;
}

internal void UIPushText(ui_context *UI, char *Text, v2 Pos, v4 Color)
{
  ui_command *Cmd = UIPushCommand(UI, UI_COMMAND_text, sizeof(ui_text_command) + strlen(Text));
  Cmd->Text.Pos = Pos;
  Cmd->Text.Color = Color;
  strncpy(Cmd->Text.Str, Text, strlen(Text));
}

internal void UIPushTexture(ui_context *UI, v4 Rect, i32 TextureID, v4 Color)
{
  ui_command *CloseIcon = UIPushCommand(UI, UI_COMMAND_icon, sizeof(ui_icon_command));
  CloseIcon->Icon.Rect = Rect;
  CloseIcon->Icon.ID = TextureID;
  CloseIcon->Icon.Color = Color;
}

///////////////////////////////////////////////////////////////////////////////

internal b32 UINextCommand(ui_context *UI, ui_command **Cmd)
{
  if (*Cmd)
  {
    *Cmd = (ui_command*)(((char*)*Cmd) + (*Cmd)->Base.SizeBytes);
  }
  else
  {
    *Cmd = (ui_command*)UI->CommandList.Items;
  }

  while ((u8*)*Cmd != UI->CommandList.Items + UI->CommandList.Index) {
    return(true);
  }
  
  return(false);
}

///////////////////////////////////////////////////////////////////////////////

internal i32 PoolInit(ui_context *UI, ui_pool_item *Pool, u32 PoolSize, ui_id ID)
{
  // Find the least recently used item in the pool
  i32 Result = -1;
  u64 OldestFrame = UI->Frame;
  foreach (I, PoolSize)
  {
    if (Pool[I].LastUsed < OldestFrame)
    {
      Result = I;
      OldestFrame = Pool[I].LastUsed;
    }
  }

  Assert(Result > -1);

  Pool[Result].ID = ID;
  Pool[Result].LastUsed = UI->Frame;
  return(Result);
}

internal i32 PoolGet(ui_context *UI, ui_pool_item *Pool, u32 PoolSize, ui_id ID)
{
  UNUSED(UI);

  foreach(I, PoolSize)
  {
    if (Pool[I].ID == ID)
    {
      return(I);
    }
  }

  return(-1);
}

internal void PoolUpdate(ui_context *UI, ui_pool_item *Pool, i32 ItemIndex)
{
  Pool[ItemIndex].LastUsed = UI->Frame;
}

///////////////////////////////////////////////////////////////////////////////

internal ui_id UIGetID(ui_context *UI, char *Data, u32 Size)
{
  ui_id HashValue = StackPeek(UI->IDStack, FNV1A_HASH_INITIAL);
  Hash(&HashValue, (u8*)Data, Size);
  return(HashValue);
}

internal ui_container* UIGetContainer(ui_context *UI, ui_id ID)
{
  // Check if container for this ID already exists
  i32 Index = PoolGet(UI, UI->ContainerPool, UI_CONTAINER_POOL_SIZE, ID);
  if (Index >= 0)
  {
    if (UI->Containers[Index].IsOpen)
    {
      PoolUpdate(UI, UI->ContainerPool, Index);
    }

    return(UI->Containers + Index);
  }

  // Otherwise, allocate a new container for this ID
  Index = PoolInit(UI, UI->ContainerPool, UI_CONTAINER_POOL_SIZE, ID);
  ui_container *Container = UI->Containers + Index;
  memset(Container, 0, sizeof(ui_container));
  Container->IsOpen = true;
  UIBringToFront(UI, Container);

  return(Container);
}

///////////////////////////////////////////////////////////////////////////////

internal void UICreate(ui_context *UI, font *Font)
{
  UI->KeyboardInputConsumed = false;
  UI->MouseInputConsumed = false;
  UI->TextInputConsumed = false;
  UI->Font = Font;
  UI->Frame = 0;
}

internal void UIBegin(ui_context *UI)
{
  UI->KeyboardInputConsumed = false;
  UI->MouseInputConsumed = false;
  UI->TextInputConsumed = false;

  UI->CommandList.Index = 0;
  UI->Frame++;
}

internal void UIEnd(ui_context *UI)
{
  
}

internal i32 UIBeginWindow(ui_context *UI, char *Title, v4 Rect)
{
  ui_id ID = UIGetID(UI, Title, strlen(Title));
  ui_container *Container = UIGetContainer(UI, ID);

  // If the retained container state indicates that this window was closed then
  // do nothing.
  if (!Container || !Container->IsOpen)
  {
    // TODO: Provide a way to re-open a previously closed window?
    return(0);
  }

  // Push window ID onto the ID stack so that everything contained within it is
  // hashed relative to the window ID. This prevents, for example, conflicts
  // between identically named buttons inside of separate windows. This also
  // allows us to use static tags like !title and !scrollbarx for component
  // parts of the window itself.
  StackPush(UI->IDStack, ID);

  // If this is the first time this container was allocated, then initialize
  // its rect. After this first initialization the rect size is ignored.
  if (Container->Rect.Width == 0)
  {
    Container->Rect = Rect;
  }
  
  // Window border
  UIPushRect(UI, Rect, V4(0.098, 0.098, 0.098, 1));
  // Window content area
  UIPushRect(UI, ExpandRect(Rect, -1), V4(0.196, 0.196, 0.196, 1));

  f32 TextHeight = FontTextHeightPixels(UI->Font);

  v4 TitleRect = V4(Rect.X, Rect.Y + Rect.Height - TextHeight - 5, Rect.Width, TextHeight + 10);
  UIPushRect(UI, TitleRect, V4(0.098, 0.098, 0.098, 1));

  UIPushText(UI, Title, V2(Rect.X + 5, TitleRect.Y + 9), V4(0.941, 0.941, 0.941, 1));

  v4 CloseIconRect = V4(Rect.X + Rect.Width - 32 - 2, Rect.Y + Rect.Height - 32, 32, 32);
  UIPushTexture(UI, CloseIconRect, UI_ICON_close, V4(1));

  return(1);
}

internal void UIEndWindow(ui_context *UI)
{
  StackPop(UI->IDStack);
}

internal void UIBringToFront(ui_context *UI, ui_container *Container)
{
  Container->ZIndex = UI->LastZIndex++;
}

internal void UIRender(ui_context *UI, renderer *Renderer, texture_catalog *TextureCatalog, platform_state *Platform)
{
  Renderer2DRightHanded(Renderer, Platform->Input.WindowDim);
  ui_command *Cmd = NULL;
  while (UINextCommand(UI, &Cmd))
  {
    switch (Cmd->Type)
    {
    case UI_COMMAND_rect:
    {
      RendererPushFilledRect(
        Renderer,
        0,
        Cmd->Rect.Rect,
        Cmd->Rect.Color
      );
    }
    break;
    case UI_COMMAND_text:
    {
      RendererPushText(
        Renderer,
        0,
        UI->Font,
        Cmd->Text.Str,
        Cmd->Text.Pos,
        Cmd->Text.Color
      );
    }
    break;
    case UI_COMMAND_icon:
    {
      texture Icons = TextureCatalogGet(TextureCatalog, Platform, "ui_icons");
      RendererPushTexture(
        Renderer,
        0,
        Icons,
        V4(0, 0, 16, 16),
        Cmd->Icon.Rect,
        Cmd->Icon.Color
      );
    }
    break;
    default: break;
    }
  }
  RendererPopMVPMatrix(Renderer);
}

internal button_style DefaultButtonStyle = {
  .BackgroundColor = V4(0.196, 0.196, 0.196, 1),
  .HoverBackgroundColor = V4(0.296, 0.296, 0.296, 1),
  .TextColor = V4(1, 1, 1, 1),
  .Font = NULL
};

internal b32 DrawRectButton(renderer *Renderer, app_context Ctx, v4 Rect, button_style Style)
{
  b32 Result = false;

  v4 Color = Style.BackgroundColor;
  if (RectPointIntersect(Rect, Ctx.Game->MousePos))
  {
    Color = Style.HoverBackgroundColor;
    if (MousePressed(Ctx.Platform, MOUSE_BUTTON_left))
    {
      Result = true;
    }
  }

  Renderer2DRightHanded(Renderer, Ctx.Game->RenderDim);
  RendererPushFilledRect(
    Renderer,
    0,
    Rect,
    Color
  );
  RendererPopMVPMatrix(Renderer);

  return(Result);
}

internal b32 DrawSpriteButton(renderer *Renderer, app_context Ctx, sprite Sprite, v4 Rect, button_style Style)
{
  b32 Result = false;

  v4 Color = Style.BackgroundColor;
  if (RectPointIntersect(Rect, Ctx.Game->MousePos))
  {
    Color = Style.HoverBackgroundColor;
    if (MousePressed(Ctx.Platform, MOUSE_BUTTON_left))
    {
      Result = true;
    }
  }

  Renderer2DRightHanded(Renderer, Ctx.Game->RenderDim);
  RendererPushTexture(
    Renderer,
    0,
    Sprite.Texture,
    Sprite.Source,
    Rect,
    Color
  );
  RendererPopMVPMatrix(Renderer);

  return(Result);
}

internal b32 DrawButton(renderer *Renderer, app_context Ctx, char *Title, v4 Rect, button_style Style)
{
  b32 Pressed = DrawRectButton(Renderer, Ctx, Rect, Style);

  if (Style.Font)
  {
    f32 TextWidth = FontTextWidthPixels(Style.Font, Title);
    v2 TextPos = V2(Rect.X + Rect.Width/2 - TextWidth / 2, Rect.Y + FontCenterOffset(Style.Font, Rect.Height));
    Renderer2DRightHanded(Renderer, Ctx.Game->RenderDim);
    RendererPushText(
      Renderer,
      0,
      Style.Font,
      Title,
      TextPos,
      Style.TextColor
    );
    RendererPopMVPMatrix(Renderer);
  }

  return(Pressed);
}

internal b32 DrawCheckbox(renderer *Renderer, app_context Ctx, texture_catalog *TextureCatalog, char *Title, v4 Rect, button_style Style, b32 *Value)
{
  texture UIIcons = TextureCatalogGet(TextureCatalog, Ctx.Platform, "ui_icons");
  sprite CheckboxSprite = Sprite(UIIcons, V4(64, 0, 16, 16));
  if (*Value)
  {
    CheckboxSprite = Sprite(UIIcons, V4(80, 0, 16, 16));
  }

  b32 Pressed = DrawSpriteButton(Renderer, Ctx, CheckboxSprite, Rect, Style);
  if (Pressed)
  {
    *Value = !*Value;
  }

  if (Style.Font)
  {
    v2 TextPos = V2(Rect.X + Rect.Width * 1.2, Rect.Y + FontCenterOffset(Style.Font, Rect.Height));
    Renderer2DRightHanded(Renderer, Ctx.Game->RenderDim);
    RendererPushText(
      Renderer,
      0,
      Style.Font,
      Title,
      TextPos,
      Style.TextColor
    );
    RendererPopMVPMatrix(Renderer);
  }

  return(Pressed);
}

///////////////////////////////////////////////////////////////////////////////
// Experimental stuff below
///////////////////////////////////////////////////////////////////////////////

internal void UIStateInit(ui_state *UI, font *Font, texture_catalog *TextureCatalog, renderer* Renderer)
{
  UI->Font = Font;
  UI->TextureCatalog = TextureCatalog;
  UI->Renderer = Renderer;
  UI->Style = {
    32.0f, 32.0f,
    V4(0.35, 0.35, 0.35, 1),
    V4(0.7, 0.198, 0.198, 1),
    {V4(0, 0, 0, 1), V4(0.298, 0.298, 0.298, 1), V4(1, 1, 1, 1), UI->Font},
    {V4(0.198, 0.198, 0.4, 1), V4(0.198, 0.198, 0.7, 1), V4(1, 1, 1, 1), UI->Font}
  };
}

internal void BeginWidgets(ui_state *UI, app_context Ctx)
{
  UI->NextHot = WIDGET_ID_none;
  UI->NextActive = WIDGET_ID_none;
  UI->MousePos = Ctx.Game->MousePos;
  UI->MouseDelta = UI->MousePos - UI->LastMousePos;
  UI->ActiveClip = V4(-1, -1, -1, -1);
}

internal void EndWidgets(ui_state *UI)
{
  UI->Hot = UI->NextHot;
  UI->Active = UI->NextActive;
  UI->CurrentParent = WIDGET_ID_none;
  UI->LastMousePos = UI->MousePos;
}

internal b32 WidgetWindowBegin(ui_state *UI, app_context Ctx, v4 Rect, char *Title, ui_window *Window)
{
  i32 ParentID = (i32)FNV1A_HASH_INITIAL;
  Hash((u32*)&ParentID, (u8*)Title, strlen(Title));
  widget_id WindowID = {ParentID, -1, 0};
  widget_id TitleBarID = {ParentID, -1, 1};
  widget_id ResizeID = {ParentID, -1, 2};

  if (Window->IsOpen)
  {
    if (UI->Active == TitleBarID)
    {
      Window->Offset += UI->MouseDelta;
    }
    else if (UI->Active == ResizeID)
    {
      Window->SizeOffset += UI->MouseDelta;
    }

    if (UI->Hot == TitleBarID)
    {
      if (MouseDown(Ctx.Platform, MOUSE_BUTTON_left))
      {
        UI->NextActive = TitleBarID;
      }
      else
      {
        UI->NextActive = WIDGET_ID_none;
      }
    }
    else if (UI->Hot == ResizeID)
    {
      if (MouseDown(Ctx.Platform, MOUSE_BUTTON_left))
      {
        UI->NextActive = ResizeID;
      }
      else
      {
        UI->NextActive = WIDGET_ID_none;
      }
    }

    Rect.Width += Window->SizeOffset.X;
    Rect.Height = Round(Rect.Height - Window->SizeOffset.Y);
    Rect.Y += Window->SizeOffset.Y;

    UI->CurrentParent = WindowID;
    UI->ParentOffset = V4(V2(Window->Offset), V2(0, 0));
    UI->ActiveClip = Rect + UI->ParentOffset;

    v4 TitleBarRect = V4(Rect.X, Rect.Y + Rect.Height - UI->Style.TitleBarHeight, Rect.Width, UI->Style.TitleBarHeight) + UI->ParentOffset;
    v4 WindowRect = V4(Rect.X, Rect.Y, Rect.Width, Rect.Height - UI->Style.TitleBarHeight) + UI->ParentOffset;

    texture Icons = TextureCatalogGet(UI->TextureCatalog, Ctx.Platform, "ui_icons");
    sprite CloseButtonSprite = Sprite(Icons, V4(0, 0, 16, 16));
    v4 CloseButtonRect = ExpandRect(V4(TitleBarRect.X + TitleBarRect.Width - 32, TitleBarRect.Y, 32, 32), -4);

    sprite ResizeButtonSprite = Sprite(Icons, V4(0, 17, 16, 16));
    v4 ResizeRect = V4(WindowRect.X + WindowRect.Width - 32, WindowRect.Y, 32, 32);

    UI->LayoutNext = ExpandRect(WindowRect, -2);
    UI->LayoutNext.Y += UI->LayoutNext.Height - 2;

    if (RectPointIntersect(ResizeRect, UI->MousePos))
    {
      UI->NextHot = ResizeID;
    }
    else if (RectPointIntersect(TitleBarRect, UI->MousePos))
    {
      UI->NextHot = TitleBarID;
    }
    else if (RectPointIntersect(WindowRect, UI->MousePos))
    {
      UI->NextHot = WindowID;
    }

    Renderer2DRightHanded(UI->Renderer, Ctx.Game->RenderDim);

    RendererPushClip(
      UI->Renderer,
      MapRectToResolution(
        Rect + UI->ParentOffset,
        V2(Ctx.Game->RenderDim),
        V2(Ctx.Platform->Input.WindowDim)));

    RendererPushFilledRect(UI->Renderer, 0, WindowRect, UI->Style.WindowColor);
    RendererPushFilledRect(UI->Renderer, 0, TitleBarRect, UI->Style.TitleBarColor);
    
    v2 TextPos = V2(TitleBarRect.X + 5, TitleBarRect.Y + FontCenterOffset(UI->Font, TitleBarRect.Height));
    RendererPushText(
      UI->Renderer,
      0,
      UI->Font,
      Title,
      TextPos,
      V4(1, 1, 1, 1)
    );
    RendererPopMVPMatrix(UI->Renderer);

    if (DrawSpriteButton(UI->Renderer, Ctx, ResizeButtonSprite, V4(WindowRect.X + WindowRect.Width - 32, WindowRect.Y, 32, 32), UI->Style.Button))
    {
      // Do nothing
    }

    if (DrawSpriteButton(UI->Renderer, Ctx, CloseButtonSprite, CloseButtonRect, UI->Style.CloseButton))
    {
      Window->IsOpen = false;
    }
  }

  return(Window->IsOpen);
}

internal void WidgetWindowEnd(ui_state *UI)
{
  UI->CurrentParent = WIDGET_ID_none;
  UI->ParentOffset = V4(0, 0, 0, 0);
  UI->ActiveClip = V4(-1, -1, -1, -1);
  RendererPopClip(UI->Renderer);
}

internal b32 WidgetButton(ui_state *UI, app_context Ctx, v4 Rect, char *Text)
{
  b32 Result = false;

  u32 ID = FNV1A_HASH_INITIAL;
  Hash(&ID, (u8*)Text, strlen(Text));
  widget_id WidgetID = {UI->CurrentParent.ParentID, (i32)ID, 0};

  if (UI->CurrentParent.ParentID != -1)
  {
    Rect.Height = UI->Style.ButtonHeight;
    Rect.X = UI->LayoutNext.X;
    Rect.Y = UI->LayoutNext.Y - Rect.Height;
    Rect.Width = UI->LayoutNext.Width;
    UI->LayoutNext.Y -= Rect.Height + 2;
  }
  else
  {
    Rect = Rect + UI->ParentOffset;
  }

  v4 ClipRect = Rect;
  if (UI->ActiveClip.X != -1)
  {
    ClipRect = IntersectRects(Rect, UI->ActiveClip);
  }

  v4 Color = UI->Style.Button.BackgroundColor;
  if (RectPointIntersect(ClipRect, Ctx.Game->MousePos))
  {
    UI->NextHot = WidgetID;
  }

  if (UI->Hot == WidgetID)
  {
    Color = UI->Style.Button.HoverBackgroundColor;
    if (MousePressed(Ctx.Platform, MOUSE_BUTTON_left))
    {
      Result = true;
      UI->Active = WidgetID;
    }
  }

  Renderer2DRightHanded(UI->Renderer, Ctx.Game->RenderDim);
  {
    RendererPushFilledRect(
      UI->Renderer,
      0,
      Rect,
      Color
    );

    if (UI->Font)
    {
      f32 TextWidth = FontTextWidthPixels(UI->Font, Text);
      v2 TextPos = V2(
        Rect.X + Rect.Width/2 - TextWidth / 2,
        Rect.Y + FontCenterOffset(UI->Font, Rect.Height));
      if (UI->CurrentParent.ParentID == -1)
      {
        TextPos += UI->ParentOffset.XY;
      }
      RendererPushText(
        UI->Renderer,
        0,
        UI->Font,
        Text,
        TextPos,
        UI->Style.Button.TextColor
      );
    }
  }
  RendererPopMVPMatrix(UI->Renderer);

  return(Result);
}

internal b32 WidgetSpriteButton(ui_state *UI, app_context Ctx, sprite Sprite, v4 Rect) {
  b32 Result = false;

  u32 ID = FNV1A_HASH_INITIAL;
  Hash(&ID, (u8*)&Sprite, sizeof(sprite));
  Hash(&ID, (u8*)&Rect, sizeof(v4));
  widget_id WidgetID = {UI->CurrentParent.ParentID, (i32)ID, 0};

  if (UI->CurrentParent.ParentID != -1)
  {
    Rect.X = UI->LayoutNext.X;
    Rect.Y = UI->LayoutNext.Y - Rect.Height;
    UI->LayoutNext.Y -= Rect.Height + 2;
  }
  else
  {
    Rect = Rect + UI->ParentOffset;
  }

  v4 ClipRect = Rect;
  if (UI->ActiveClip.X != -1)
  {
    ClipRect = IntersectRects(Rect, UI->ActiveClip);
  }

  v4 Color = UI->Style.Button.BackgroundColor;
  if (RectPointIntersect(ClipRect, Ctx.Game->MousePos))
  {
    UI->NextHot = WidgetID;
  }

  if (UI->Hot == WidgetID)
  {
    Color = UI->Style.Button.HoverBackgroundColor;
    if (MousePressed(Ctx.Platform, MOUSE_BUTTON_left))
    {
      Result = true;
      UI->Active = WidgetID;
    }
  }

  Renderer2DRightHanded(UI->Renderer, Ctx.Game->RenderDim);
  {
    RendererPushTexture(
      UI->Renderer,
      0,
      Sprite.Texture,
      Sprite.Source,
      Rect,
      Color
    );
  }
  RendererPopMVPMatrix(UI->Renderer);

  return(Result);
}

internal b32 WidgetCheckbox(ui_state *UI, app_context Ctx, v4 Rect, char *Text, b32 *Value) {
  texture UIIcons = TextureCatalogGet(UI->TextureCatalog, Ctx.Platform, "ui_icons");
  sprite CheckboxSprite = Sprite(UIIcons, V4(64, 0, 16, 16));
  if (*Value)
  {
    CheckboxSprite = Sprite(UIIcons, V4(80, 0, 16, 16));
  }

  v4 SpriteRect = V4(
    UI->LayoutNext.X,
    UI->LayoutNext.Y,
    UI->Style.ButtonHeight * 0.8,
    UI->Style.ButtonHeight * 0.8
  );

  b32 Pressed = WidgetSpriteButton(UI, Ctx, CheckboxSprite, SpriteRect);

  if (Pressed) {
    *Value = !*Value;
  }

  Renderer2DRightHanded(UI->Renderer, Ctx.Game->RenderDim);
  {
    if (UI->Font)
    {
      f32 TextWidth = FontTextWidthPixels(UI->Font, Text);
      v2 TextPos = V2(
        SpriteRect.X + SpriteRect.Width + 5,
        SpriteRect.Y - SpriteRect.Height + FontCenterOffset(UI->Font, SpriteRect.Height)
      );
      if (UI->CurrentParent.ParentID == -1)
      {
        TextPos += UI->ParentOffset.XY;
      }
      RendererPushText(
        UI->Renderer,
        0,
        UI->Font,
        Text,
        TextPos,
        UI->Style.Button.TextColor
      );
    }
  }
  RendererPopMVPMatrix(UI->Renderer);

  return(Pressed);
}
