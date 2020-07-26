#ifndef UI_H
#define UI_H

#include "common/language_layer.h"

typedef struct ui_context {
  b32 WantKeyboardInput;
  b32 WantMouseInput;
} ui_context;

internal void UICreate(ui_context *UI);
internal void UIBegin(ui_context *UI);
internal void UIEnd(ui_context *UI);

#endif //UI_H
