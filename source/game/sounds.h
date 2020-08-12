#ifndef GAME_SOUNDS_H
#define GAME_SOUNDS_H

// NOTE: All game audio is compressed Ogg-Vorbis format data that we decode on
// the fly when playing it back.
#include "ext/stb_vorbis.c"

typedef struct sound {
  b32 Loaded;
  platform_entire_file SoundFile;

  u32 Channels;
  u32 SampleRate;
  u32 Samples;
  u32 DataLength;
  u8* Data;
} sound;

typedef struct sound_manager {
  const char *SoundDirectory;
} sound_manager;

internal b32 SoundManagerInit(sound_manager *SoundManager, const char *SoundDirectory);
internal void SoundManagerDestroy(sound_manager *SoundManager);
internal b32 SoundManagerLoadSound(sound_manager *SoundManager, sound *Sound, platform_state *Platform, const char *SoundFile);
internal void SoundManagerDestroySound(sound_manager *SoundManager, sound *Sound, platform_state *Platform);

#endif // GAME_SOUNDS_H
