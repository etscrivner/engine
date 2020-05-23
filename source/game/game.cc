#include <stdio.h>

#include "game.h"
#include "common/language_layer.h"

extern "C" {
  void Update(platform_state *Platform, f32 DeltaTimeSecs)
  {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
  }

  void Shutdown(platform_state *Platform)
  {}

  void OnFrameStart(platform_state *Platform)
  {}

  void OnFrameEnd(platform_state *Platform)
  {}
}
