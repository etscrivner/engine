#include "ui/debug_console.h"

const f32 ConsoleHeightPixels = 400;

internal void CommandCamera(console *Console, app_context Ctx, char *Args)
{
  if (Args != NULL)
  {
    if (strcmp(Args, "debug") == 0) {
      Ctx.Game->ShowCameraDebug = !Ctx.Game->ShowCameraDebug;
      ConsoleLogf(Console, "Camera Debug: %s", Ctx.Game->ShowCameraDebug ? "on" : "off");
    } else if (strcmp(Args, "recenter") == 0) {
      Ctx.Game->Camera.RecenterOn = !Ctx.Game->Camera.RecenterOn;
      ConsoleLogf(Console, "Camera Recenter: %s", Ctx.Game->Camera.RecenterOn ? "on" : "off");
    }
  }
}

internal void CommandMap(console *Console, app_context Ctx, char *Args)
{
  if (Args != NULL)
  {
    if (strcmp(Args, "debug") == 0) {
      Ctx.Game->ShowMapDebug = !Ctx.Game->ShowMapDebug;
      ConsoleLogf(Console, "Map Debug: %s", Ctx.Game->ShowMapDebug ? "on" : "off");
    }
  }
}

internal console_style DefaultConsoleStyle = {
  .ThumbPadding = 2.0f,
  .Colors = {
    [CONSOLE_COLOR_scrollbar_bg] = V4(0.0, 0.2, 0.8, 1.0),
    [CONSOLE_COLOR_scrollbar_thumb] = V4(0.0, 0.0, 0.5, 1.0)
  }
};

internal console_command ConsoleCommands[] = {
  { .Command = "camera", .Cmd = CommandCamera },
  { .Command = "map", .Cmd = CommandMap }
};

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleCreate(console *Console, font *Font, memory_arena *TransientArena)
{
  Console->Mode = CONSOLE_MODE_unloaded;
  // NOTE: Copy here to avoid memory access issues on reload which could lead
  // to a segfault.
  memcpy(&Console->Style, &DefaultConsoleStyle, sizeof(console_style));
  Console->Font = Font;
  Console->YScrollOffset = 0.0f;
  Console->ThumbFocus = false;
  Console->TextHeight = FontTextHeightPixels(Font);
  Console->YOffset = ConsoleHeightPixels;
  Console->TransientArena = TransientArena;
  Console->KeyboardInputConsumed = false;
  Console->MouseInputConsumed = false;
  Console->TextInputConsumed = false;
}

///////////////////////////////////////////////////////////////////////////////

internal b32 ConsoleIsActive(console *Console)
{
  return(Console->Mode == CONSOLE_MODE_loaded);
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleInputDeleteRange(console *Console, u32 Start, u32 End)
{
  Assert(Start < End);
  u32 TextLength = strlen(Console->Input);
  Assert(End <= TextLength);
  
  if (TextLength > 0)
  {
    for (u32 I = 0; I < (TextLength - End) + 1; ++I)
    {
      Console->Input[Start + I] = Console->Input[End + I];
    }
    Console->CursorPos = Start;
  }
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleInputInsert(console *Console, const char *Text)
{
  // TODO: Add more bounds and limit checking here...
  u32 TextLength = strlen(Console->Input);
  u32 InputTextLength = strlen(Text);
  
  if (InputTextLength > 0)
  {
    if (Console->CursorPos < TextLength)
    {
      // Insert in the middle
      for (u32 I = TextLength; I >= Console->CursorPos; --I)
      {
        Console->Input[I + InputTextLength] = Console->Input[I];
        if (I == 0)
        {
          break;
        }
      }
      
      for (u32 I = 0; I < InputTextLength; ++I)
      {
        Console->Input[Console->CursorPos + I] = Text[I];
      }
      
      Console->CursorPos += InputTextLength;
    }
    else
    {
      strncat(Console->Input, Text, 32);
      Console->CursorPos += InputTextLength;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleLog(console *Console, const char *Text)
{
  if (Console->LogLineCount >= ArrayCount(Console->Log)) {
    for (u32 I = 1; I < Console->LogLineCount; ++I)
    {
      strncpy(Console->Log[I - 1], Console->Log[I], 64);
    }
    
    Console->LogLineCount -= 1;
  }
  
  strncpy(Console->Log[Console->LogLineCount++], Text, 64);
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleLogf(console *Console, const char *Fmt, ...)
{
  local_persist char Output[256];
  va_list List;
  va_start(List, Fmt);
  vsnprintf(Output, 256, Fmt, List);
  va_end(List);

  ConsoleLog(Console, Output);
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleRender(console *Console, renderer *Renderer)
{
  f32 TextHeight = Console->TextHeight;
  v2u RenderDim = Console->RenderDim;
  f32 YOffset = Console->YOffset;

  if (Console->Mode == CONSOLE_MODE_unloaded)
  {
    return;
  }

  // NOTE: Clipping rectangles are defined in window coordinates not projective
  // space coordinates. This poses a problem for us as our UI is currently
  // drawn in projective space coordinates. In order to ensure that the UI is
  // always drawn and clipped correctly, we have two options:
  //
  //   1. Define the projective coordinates for UI elements to always match
  //      the window coordinates. Thereby making clipping trivial.
  //   2. Map clipping rectangles from projective to window coordinates and
  //      do some rounding to ensure that they are always whole numbers
  //      avoiding cracks and artifacts as much as possible.
  //
  // Here we've opted for a combination of solutions. Namely, we combine the
  // following:
  //
  //    1. The render resolution and window size are not allowed to be
  //       arbitrary, but instead are chosen from a fixed set of allowed
  //       dimensions.
  //    2. All clipping rectangles are mapped from projective space
  //       dimensions to window dimensions. All mouse coordinates are
  //       mapped from window dimensions to projective space dimensions.
  Renderer2DRightHanded(Renderer, RenderDim);

  // Render console background
  RendererPushFilledRect(
    Renderer,
    0,
    V4(0, RenderDim.Height - ConsoleHeightPixels + TextHeight + YOffset, RenderDim.Width, ConsoleHeightPixels - TextHeight),
    V4(0.8, 0.8, 0.8, 0.8));
  // Render input line background
  RendererPushFilledRect(
    Renderer,
    0,
    V4(0, RenderDim.Height - ConsoleHeightPixels + YOffset, RenderDim.Width, TextHeight),
    V4(0.5, 0.5, 0.5, 0.8)
  );

  // Render scrollbar if text exceeds content area size
  f32 LogHeight = Console->LogLineCount * TextHeight;
  f32 YOverflow = LogHeight - (ConsoleHeightPixels - TextHeight);
  if (YOverflow > 0.0f)
  {
    RendererPushFilledRect(Renderer, 0, Console->ScrollBarRect, Console->Style.Colors[CONSOLE_COLOR_scrollbar_bg]);
    RendererPushFilledRect(Renderer, 0, Console->ThumbRect, Console->Style.Colors[CONSOLE_COLOR_scrollbar_thumb]);
  }

  // Render cursor
  f32 CursorOffset = FontTextRangeWidthPixels(Console->Font, Console->Input, 0, Console->CursorPos);
  if (Console->SelectionStart == Console->SelectionEnd)
  {
#if 1 // Rect cursor
    
    RendererPushFilledRect(Renderer,
                           0,
                           V4(CursorOffset + 2, RenderDim.Height - ConsoleHeightPixels + YOffset, FontTextWidthPixels(Console->Font, "B"), FontTextHeightPixels(Console->Font)),
                           V4(1, 1, 1, 0.5));
#else // Line cursor
    RendererPushLine(Renderer,
                     0,
                     V2(CursorOffset + 2, RenderDim.Height - ConsoleHeightPixels + YOffset - FontDescenderPixels(Console->Font)), 
                     V2(CursorOffset + 2, RenderDim.Height - ConsoleHeightPixels + YOffset + FontAscenderPixels(Console->Font)),
                     V4(1, 1, 1, 1));
#endif
  }

  // Render text log
  v4 LogAreaClip = V4(
    0,
    RenderDim.Height - ConsoleHeightPixels + YOffset + TextHeight,
    RenderDim.Width,
    ConsoleHeightPixels
  );
  RendererPushClip(
    Renderer,
    MapRectToResolution(LogAreaClip, V2(Console->RenderDim), V2(Console->WindowDim))
  );
  {
    // Render the console log
    f32 LogYOffset = Console->LogLineCount * FontTextHeightPixels(Console->Font);
    foreach(I, Console->LogLineCount)
    {
      RendererPushText(Renderer,
                       0,
                       Console->Font,
                       Console->Log[I],
                       V2(0, RenderDim.Height - ConsoleHeightPixels + YOffset + LogYOffset - FontDescenderPixels(Console->Font) - Console->YScrollOffset),
                       V4(1, 1, 1, 1));
      LogYOffset -= FontTextHeightPixels(Console->Font);
    }
  }
  RendererPopClip(Renderer);

  v4 InputLineClip = V4(
    0,
    RenderDim.Height - ConsoleHeightPixels + YOffset,
    RenderDim.Width,
    TextHeight);
  RendererPushClip(
    Renderer,
    MapRectToResolution(InputLineClip, V2(Console->RenderDim), V2(Console->WindowDim))
  );
  // Render input line
  {
    // Render currently typed text
    if (Console->Input[0] != '\0') {
      RendererPushText(Renderer, 
                       0,
                       Console->Font,
                       Console->Input,
                       V2(0, RenderDim.Height - ConsoleHeightPixels + YOffset - FontDescenderPixels(Console->Font)), 
                       V4(1, 1, 1, 1));
    }
  
    // Render Selection
    if (Console->SelectionStart != Console->SelectionEnd)
    {
      f32 StartX = FontTextRangeWidthPixels(Console->Font, Console->Input, 0, Console->SelectionStart);
      f32 SelectionWidth = FontTextRangeWidthPixels(Console->Font, Console->Input, Console->SelectionStart, Console->SelectionEnd);
    
      RendererPushFilledRect(Renderer,
                             0,
                             V4(StartX, RenderDim.Height - ConsoleHeightPixels + YOffset, SelectionWidth, FontTextHeightPixels(Console->Font)),
                             V4(1, 1, 1, 0.8));
    }
  }
  RendererPopClip(Renderer);

  RendererPopMVPMatrix(Renderer);
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleUpdateScrollbar(console *Console, app_context Ctx, f32 YOffset)
{
  f32 ScrollBarWidth = 50;
  // Calculate updated scrollbar rect sizes
  Console->ScrollBarRect = V4(
    Console->RenderDim.Width - ScrollBarWidth,
    Console->RenderDim.Height - ConsoleHeightPixels + Console->TextHeight + YOffset,
    ScrollBarWidth, /* Width */
    ConsoleHeightPixels - Console->TextHeight /* Height */
  );
  Console->ThumbRect = ExpandRect(Console->ScrollBarRect, -Console->Style.ThumbPadding);

  f32 LogHeight = Console->LogLineCount * Console->TextHeight;
  f32 YOverflow = LogHeight - (ConsoleHeightPixels - Console->TextHeight);

  // Clamp the scroll amount to its limits
  if (Console->ThumbFocus && MouseDown(Ctx.Platform, MOUSE_BUTTON_left)) {
    // Subtract here as we need to invert the offsets since the thumb
    // starts on the bottom.
    Console->YScrollOffset -= Console->MouseDelta.Y * (LogHeight / Console->ScrollBarRect.Height);
    Console->MouseInputConsumed = true;
  }

  // Only set the scroll offset if there is YOverflow, as otherwise this will
  // cause things to vacilate between frames since there is a negative
  // overflow.
  if (YOverflow > 0.0f)
  {
    Console->YScrollOffset = Clamp(Console->YScrollOffset, 0, YOverflow);
  }

  // Compute the height of the thumb rect as occupying the range from
  // [100.0f, ScrollBarRect.Height] depending on the size of the content
  // area (The larger the size, the smaller the thumb down to the min of
  // 100.0f).
  Console->ThumbRect.Height = Max(100.0f, (Console->ScrollBarRect.Height * Console->ScrollBarRect.Height) / LogHeight);

  // Compute the offset for the scroll bar thumb as the scroll offset times
  // the ratio of thumb offset per unit scroll offset.
  Console->ThumbRect.Y += Console->YScrollOffset * (Console->ScrollBarRect.Height - Console->ThumbRect.Height - 2 * Console->Style.ThumbPadding) / YOverflow;

  if (RectPointIntersect(Console->ThumbRect, Console->Mouse)) {
    Console->ThumbFocus = true;
  } else {
    Console->ThumbFocus = false;
  }

  Console->YOffset = YOffset;
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleRunCommand(console *Console, app_context Ctx, char* CommandLine)
{
  char *Command = strtok(CommandLine, " ");
  foreach(I, ArrayCount(ConsoleCommands)) {
    if (strcmp(ConsoleCommands[I].Command, Command) == 0) {
      ConsoleCommands[I].Cmd(Console, Ctx, strtok(NULL, " "));
    }
  }
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleUpdate(console *Console, app_context Ctx, u64 DeltaTimeMicros)
{
  const f32 LoadTimeMicros = 100 * 1000;
  Console->RenderDim = Ctx.Game->RenderDim;
  Console->WindowDim = Ctx.Platform->Input.WindowDim;
  Console->KeyboardInputConsumed = false;
  Console->MouseInputConsumed = false;
  Console->TextInputConsumed = false;

  switch (Console->Mode)
  {
    case CONSOLE_MODE_loading:
    {
      //Console->TimePassed += 1/60.0f;
      Console->TimePassedMicros += DeltaTimeMicros;
      
      f32 Progress = Clamp01((f32)Console->TimePassedMicros / LoadTimeMicros);
      
      f32 Offset = EaseInQuint(ConsoleHeightPixels, 0, Progress);
      ConsoleUpdateScrollbar(Console, Ctx, Offset);
      
      if (Progress >= 1.0f)
      {
        Console->Mode = CONSOLE_MODE_loaded;
        Console->TimePassedMicros = 0;
      }
    }
    break;
    case CONSOLE_MODE_loaded:
    {
      Console->MouseDelta = (Console->Mouse - Ctx.Game->MousePos);
      Console->Mouse = Ctx.Game->MousePos;

      // Console always consumed keyboard and text input
      Console->KeyboardInputConsumed = true;
      Console->TextInputConsumed = true;

      // Console only consumes mouse input if mouse is in console rect
      v4 ConsoleRect = V4(
        0,
        Console->RenderDim.Height - ConsoleHeightPixels,
        Console->RenderDim.Width,
        ConsoleHeightPixels
      );

      if (RectPointIntersect(ConsoleRect, Console->Mouse))
      {
        Console->MouseInputConsumed = true;

        // Scroll when the mouse wheel is rolled
        Console->YScrollOffset += 15.0f * Ctx.Platform->Input.Mouse.Wheel.Y;
      }

      if (KeyPressed(Ctx.Platform, KEY_tilde))
      {
        Console->Mode = CONSOLE_MODE_unloading;
      }
      else 
      {
        if (KeyPressed(Ctx.Platform, KEY_enter))
        {
          ConsoleLog(Console, Console->Input);
          ConsoleRunCommand(Console, Ctx, Console->Input);
          Console->Input[0] = '\0';
          Console->CursorPos = 0;
        }
        else if (KeyDown(Ctx.Platform, KEY_ctrl) && KeyPressed(Ctx.Platform, KEY_a))
        {
          // Ctrl-A: Jump to beginning of console input
          Console->CursorPos = 0;
        }
        else if (KeyDown(Ctx.Platform, KEY_ctrl) && KeyPressed(Ctx.Platform, KEY_e))
        {
          // Ctrl-E: Jump to end of console input
          Console->CursorPos = strlen(Console->Input);
        }
        else if (KeyDown(Ctx.Platform, KEY_ctrl) && KeyPressed(Ctx.Platform, KEY_k))
        {
          // Ctrl-K: Kill to end of line
          Console->Input[Console->CursorPos] = '\0';
        }
        else if (KeyDown(Ctx.Platform, KEY_ctrl) && KeyPressed(Ctx.Platform, KEY_w))
        {
          // Ctrl-W: Kill to beginning of line
          u32 TextLength = strlen(Console->Input);
          for (u32 I = 0; I < Console->CursorPos; ++I)
          {
            Console->Input[I] = Console->Input[Console->CursorPos+I];
          }
          Console->CursorPos = 0;
        }
        else
        {
          ConsoleInputInsert(Console, Ctx.Platform->Input.Text);
        }
      }
      
      if (KeyPressedOrRepeat(Ctx.Platform, KEY_backspace))
      {
        if (Console->SelectionStart != Console->SelectionEnd)
        {
          ConsoleInputDeleteRange(Console, Console->SelectionStart, Console->SelectionEnd);
          Console->SelectionStart = 0;
          Console->SelectionEnd = 0;
        }
        else
        {
          u32 TextLength = strlen(Console->Input);
          if (TextLength > 0)
          {
            if (Console->CursorPos < TextLength)
            {
              for (u32 I = Console->CursorPos-1; I < TextLength; ++I)
              {
                Console->Input[I] = Console->Input[I+1];
              }
              --Console->CursorPos;
            }
            else
            {
              // Insert at the end
              Console->Input[TextLength - 1] = '\0';
              --Console->CursorPos;
            }
          }
        }
      }

      if (KeyPressedOrRepeat(Ctx.Platform, KEY_delete))
      {
        if (Console->SelectionStart != Console->SelectionEnd)
        {
          ConsoleInputDeleteRange(Console, Console->SelectionStart, Console->SelectionEnd);
          Console->SelectionStart = 0;
          Console->SelectionEnd = 0;
        }
        else
        {
          u32 TextLength = strlen(Console->Input);
          if (TextLength > 0 && Console->CursorPos < TextLength)
          {
            for (u32 I = Console->CursorPos; I < TextLength; ++I)
            {
              Console->Input[I] = Console->Input[I+1];
            }
          }
        }
      }
      
      if (KeyPressed(Ctx.Platform, KEY_left))
      {
        if (Console->CursorPos > 0) 
        {
          --Console->CursorPos;
        }
      }
      
      if (KeyPressed(Ctx.Platform, KEY_right))
      {
        if (Console->CursorPos < strlen(Console->Input))
        {
          ++Console->CursorPos;
        }
      }
      
      if (MousePressed(Ctx.Platform, MOUSE_BUTTON_middle))
      {
        scoped_arena ScopedArena(Console->TransientArena);
        char *Text = Ctx.Platform->Interface.GetClipboardText(&ScopedArena);
        ConsoleInputInsert(Console, Text);
      }

      if (MousePressed(Ctx.Platform, MOUSE_BUTTON_left))
      {
        // Check for click on input line
        f32 YMin = Console->RenderDim.Height - ConsoleHeightPixels;
        f32 YMax = Console->RenderDim.Height - ConsoleHeightPixels + FontTextHeightPixels(Console->Font);
        
        if (Console->Mouse.Y >= YMin && Console->Mouse.Y <= YMax)
        {
          i32 Index = FontTextPixelOffsetToIndex(Console->Font, Console->Input, Console->Mouse.X);
          if (Index >= 0)
          {
            Console->CursorPos = Index;
          }
          else
          {
            Console->CursorPos = strlen(Console->Input);
          }
          
          Console->SelectionStart = 0;
          Console->SelectionEnd = 0;
        }
      }
      else if (MouseDown(Ctx.Platform, MOUSE_BUTTON_left))
      {
        f32 YMin = Console->RenderDim.Height - ConsoleHeightPixels;
        f32 YMax = Console->RenderDim.Height - ConsoleHeightPixels + FontTextHeightPixels(Console->Font);
        if (Console->Mouse.Y >= YMin && Console->Mouse.Y <= YMax)
        {
          i32 Index = FontTextPixelOffsetToIndex(Console->Font, Console->Input, Console->Mouse.X);
          u32 EndPos = 0;
          if (Index >= 0)
          {
            EndPos = Index;
          }
          else
          {
            EndPos = strlen(Console->Input);
          }
          
          if (EndPos < Console->CursorPos)
          {
            Console->SelectionStart = EndPos;
            Console->SelectionEnd = Console->CursorPos;
          }
          else if (EndPos > Console->CursorPos)
          {
            Console->SelectionStart = Console->CursorPos;
            Console->SelectionEnd = EndPos;
          }
          else
          {
            Console->SelectionStart = 0;
            Console->SelectionEnd = 0;
          }
        }
       
      }
      else
      {
        if (Console->SelectionStart != Console->SelectionEnd)
        {
          scoped_arena ScopedArena(Console->TransientArena);
          char *Data = ScopedArenaPushArray(&ScopedArena, Console->SelectionEnd - Console->SelectionStart, char);
          strncpy(Data, Console->Input + Console->SelectionStart, Console->SelectionEnd - Console->SelectionStart);
          Ctx.Platform->Interface.SetClipboardText(Data);
        }
      }

      ConsoleUpdateScrollbar(Console, Ctx, 0);
    }
    break;
    case CONSOLE_MODE_unloading:
    {
      //Console->TimePassedMicros += 1/60.0f * 1000.0f;
      Console->TimePassedMicros += DeltaTimeMicros;
      
      f32 Progress = Clamp01((f32)Console->TimePassedMicros / LoadTimeMicros);
      
      f32 Offset = EaseInQuint(0, ConsoleHeightPixels, Progress);
      ConsoleUpdateScrollbar(Console, Ctx, Offset);
      
      if (Progress >= 1.0f)
      {
        Console->Mode = CONSOLE_MODE_unloaded;
        Console->TimePassedMicros = 0;
      }
    }
    break;
    case CONSOLE_MODE_unloaded:
    {
      if (KeyPressed(Ctx.Platform, KEY_tilde))
      {
        Console->Mode = CONSOLE_MODE_loading;
      }
    }
    break;
    default: break;
  }
}
