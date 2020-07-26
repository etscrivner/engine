#include "ui/debug_console.h"

internal void ConsoleCreate(console *Console, font *Font)
{
  Console->Mode = CONSOLE_MODE_unloaded;
  Console->Font = Font;
  Console->YScrollOffset = 0.0f;
  Console->ThumbFocus = false;
}

///////////////////////////////////////////////////////////////////////////////

internal b32 ConsoleIsActive(console *Console)
{
  return(Console->Mode == CONSOLE_MODE_loaded);
}

///////////////////////////////////////////////////////////////////////////////

internal void ConsoleInputMouse(console *Console, v2 MousePos)
{
  Console->MouseDelta = (Console->Mouse - MousePos);
  Console->Mouse = MousePos;
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

const f32 ConsoleHeightPixels = 400;

void ConsoleDraw(renderer *Renderer, console *Console, f32 YOffset, platform_state *Platform)
{
  f32 TextHeight = FontTextHeightPixels(Console->Font);

  // NOTE: Clipping rectangles are defined in window coordinates not projective
  // space coordinates. This poses a problem for us as our UI is currently
  // drawn in projective space coordinates. In order to ensure that the UI is
  // always drawn and clipped correctly, we need to define the projective
  // coordinates to match the window coordinates when drawing UI elements that
  // require clipping.
  //
  // In general all UI elements should simply be rendered using window
  // coordinates on the assumption they will all eventually use clipping.
  Renderer2DRightHanded(Renderer, Platform->Input.WindowDim);
  v2 RenderDim = Platform->Input.WindowDim;

  // Render text log
  RendererPushClip(
    Renderer,
    V4(0,
       RenderDim.Height - ConsoleHeightPixels + YOffset + TextHeight,
       RenderDim.Width,
       ConsoleHeightPixels)
  );
  {
    // Render console background
    RendererPushFilledRect(Renderer,
                           0,
                           V4(0, RenderDim.Height - ConsoleHeightPixels + TextHeight + YOffset, RenderDim.Width, ConsoleHeightPixels - TextHeight),
                           V4(0.8, 0.8, 0.8, 0.8));
  
    // Render scrollbar if text exceeds content area size
    f32 LogHeight = Console->LogLineCount * TextHeight;
    v4 ScrollBarBGColor = V4(0.0, 0.2, 0.8, 1.0);
    v4 ThumbColor = V4(0.0, 0.0, 0.5, 1.0);
    f32 YOverflow = LogHeight - (ConsoleHeightPixels - TextHeight);
    if (YOverflow > 0.0f)
    {
      f32 ThumbPadding = 2.0f;

      v4 ScrollBarRect = V4(
        RenderDim.Width - 30,
        RenderDim.Height - ConsoleHeightPixels + TextHeight + YOffset,
        30, /* Width */
        ConsoleHeightPixels - TextHeight /* Height */
      );
      v4 ThumbRect = V4(
        ScrollBarRect.X + ThumbPadding,
        ScrollBarRect.Y + ThumbPadding,
        ScrollBarRect.Width - (2 * ThumbPadding),
        0.0f
      );

      // Clamp the scroll amount to its limits
      if (Console->ThumbFocus && MouseDown(Platform, MOUSE_BUTTON_left)) {
        // Subtract here as we need to invert the offsets since the thumb
        // starts on the bottom.
        Console->YScrollOffset -= Console->MouseDelta.Y * (LogHeight / ScrollBarRect.Height);
      }
      Console->YScrollOffset = Clamp(Console->YScrollOffset, 0, YOverflow);

      // Compute the height of the thumb rect as occupying the range from
      // [100.0f, ScrollBarRect.Height] depending on the size of the content
      // area (The larger the size, the smaller the thumb down to the min of
      // 100.0f).
      ThumbRect.Height = Max(100.0f, (ScrollBarRect.Height * ScrollBarRect.Height) / LogHeight);

      // Compute the offset for the scroll bar thumb as the scroll offset times
      // the ratio of thumb offset per unit scroll offset.
      ThumbRect.Y += Console->YScrollOffset * (ScrollBarRect.Height - ThumbRect.Height - 2 * ThumbPadding) / YOverflow;

      if (RectPointIntersect(ThumbRect, Console->Mouse)) {
        Console->ThumbFocus = true;
      } else {
        Console->ThumbFocus = false;
      }

      RendererPushFilledRect(Renderer, 0, ScrollBarRect, ScrollBarBGColor);
      RendererPushFilledRect(Renderer, 0, ThumbRect, ThumbColor);
    }

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

  RendererPushClip(
    Renderer,
    V4(0,
       RenderDim.Height - ConsoleHeightPixels + YOffset,
       RenderDim.Width,
       TextHeight)
  );
  // Render input line
  {
    // Render input line background
    RendererPushFilledRect(Renderer,
                           0,
                           V4(0, RenderDim.Height - ConsoleHeightPixels + YOffset, RenderDim.Width, TextHeight),
                           V4(0.5, 0.5, 0.5, 0.8));

  
    // Render currently typed text
    if (Console->Input[0] != '\0') {
      RendererPushText(Renderer, 
                       0,
                       Console->Font,
                       Console->Input,
                       V2(0, RenderDim.Height - ConsoleHeightPixels + YOffset - FontDescenderPixels(Console->Font)), 
                       V4(1, 1, 1, 1));
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

void ConsoleUpdate(console *Console, game_state *GameState, platform_state *Platform, f32 DeltaTimeSecs)
{
  const f32 LoadTimeSeconds = 0.1f;
  
  switch (Console->Mode)
  {
    case CONSOLE_MODE_loading:
    {
      Console->TimePassed += 1/60.0f;
      
      f32 Progress = Clamp01(Console->TimePassed / LoadTimeSeconds);
      
      f32 Offset = EaseInQuint(ConsoleHeightPixels, 0, Progress);
      ConsoleDraw(&GameState->Renderer, Console, Offset, Platform);
      
      if (Progress >= 1.0f)
      {
        Console->Mode = CONSOLE_MODE_loaded;
        Console->TimePassed = 0.0f;
      }
    }
    break;
    case CONSOLE_MODE_loaded:
    {
      if (KeyPressed(Platform, KEY_tilde))
      {
        Console->Mode = CONSOLE_MODE_unloading;
      }
      else 
      {
        if (KeyPressed(Platform, KEY_enter))
        {
          ConsoleLog(Console, Console->Input);
          Console->Input[0] = '\0';
          Console->CursorPos = 0;
        }
        else if (KeyDown(Platform, KEY_ctrl) && KeyPressed(Platform, KEY_a))
        {
          // Ctrl-A: Jump to beginning of console input
          Console->CursorPos = 0;
        }
        else if (KeyDown(Platform, KEY_ctrl) && KeyPressed(Platform, KEY_e))
        {
          // Ctrl-E: Jump to end of console input
          Console->CursorPos = strlen(Console->Input);
        }
        else if (KeyDown(Platform, KEY_ctrl) && KeyPressed(Platform, KEY_k))
        {
          // Ctrl-K: Kill to end of line
          Console->Input[Console->CursorPos] = '\0';
        }
        else if (KeyDown(Platform, KEY_ctrl) && KeyPressed(Platform, KEY_w))
        {
          // Ctrl-W: Kill to beginning of line
          u32 TextLength = strlen(Console->Input);
          for (u32 I = 0; I < Console->CursorPos; ++I)
          {
            Console->Input[I] = Console->Input[Console->CursorPos+I];
          }
          Console->CursorPos = 0;
        }
        else if (KeyDown(Platform, KEY_ctrl) && KeyPressed(Platform, KEY_down))
        {
          Console->YScrollOffset -= 5.0f;
        }
        else if (KeyDown(Platform, KEY_ctrl) && KeyPressed(Platform, KEY_up))
        {
          Console->YScrollOffset += 5.0f;
        }
        else
        {
          ConsoleInputInsert(Console, Platform->Input.Text);
        }
      }
      
      if (KeyPressedOrRepeat(Platform, KEY_backspace))
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

      // Scroll when the mouse wheel is rolled
      Console->YScrollOffset += 15.0f * Platform->Input.Mouse.Wheel.Y;

      if (KeyPressedOrRepeat(Platform, KEY_delete))
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
      
      if (KeyPressed(Platform, KEY_left))
      {
        if (Console->CursorPos > 0) 
        {
          --Console->CursorPos;
        }
      }
      
      if (KeyPressed(Platform, KEY_right))
      {
        if (Console->CursorPos < strlen(Console->Input))
        {
          ++Console->CursorPos;
        }
      }
      
      if (MousePressed(Platform, MOUSE_BUTTON_middle))
      {
        scoped_arena ScopedArena(&GameState->TransientArena);
        char *Text = Platform->Interface.GetClipboardText(&ScopedArena);
        ConsoleInputInsert(Console, Text);
      }

      if (MousePressed(Platform, MOUSE_BUTTON_left))
      {
        // Check for click on input line
        f32 YMin = Platform->Input.RenderDim.Height - ConsoleHeightPixels;
        f32 YMax = Platform->Input.RenderDim.Height - ConsoleHeightPixels + FontTextHeightPixels(Console->Font);
        
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
      else if (MouseDown(Platform, MOUSE_BUTTON_left))
      {
        f32 YMin = Platform->Input.RenderDim.Height - ConsoleHeightPixels;
        f32 YMax = Platform->Input.RenderDim.Height - ConsoleHeightPixels + FontTextHeightPixels(Console->Font);
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
          scoped_arena ScopedArena(&GameState->TransientArena);
          char *Data = ScopedArenaPushArray(&ScopedArena, Console->SelectionEnd - Console->SelectionStart, char);
          strncpy(Data, Console->Input + Console->SelectionStart, Console->SelectionEnd - Console->SelectionStart);
          Platform->Interface.SetClipboardText(Data);
        }
      }
      
      ConsoleDraw(&GameState->Renderer, Console, 0, Platform);
    }
    break;
    case CONSOLE_MODE_unloading:
    {
      Console->TimePassed += 1/60.0f;
      
      f32 Progress = Clamp01(Console->TimePassed / LoadTimeSeconds);
      
      f32 Offset = EaseInQuint(0, ConsoleHeightPixels, Progress);
      ConsoleDraw(&GameState->Renderer, Console, Offset, Platform);
      
      if (Progress >= 1.0f)
      {
        Console->Mode = CONSOLE_MODE_unloaded;
        Console->TimePassed = 0.0f;
      }
    }
    break;
    case CONSOLE_MODE_unloaded:
    {
      if (KeyPressed(Platform, KEY_tilde))
      {
        Console->Mode = CONSOLE_MODE_loading;
      }
    }
    break;
    default: break;
  }
}
