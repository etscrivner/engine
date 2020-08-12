#ifndef GAME_MIXER_H
#define GAME_MIXER_H

#include "sounds.h"

// Maximum volume value
#define AUDIO_MAX_VOLUME 128

// Memory budget for audio player. This is used to store playing sounds and
// sets the cap on the maximum number of playing sounds.
//
// Should be strictly < PERMANENT_STORAGE_SIZE
#define AUDIO_PLAYER_ARENA_SIZE Megabytes(64)

typedef struct playing_sound {
  sound Sound;
  stb_vorbis* Decoder;

  b32 Loop;
  b32 IsPlaying;
  v2 CurrentVolume;
  v2 TargetVolume;
  v2 dCurrentVolume;

  // Used for silencing and restarting all game audio
  v2 SavedVolume;

  u32 NumDecodedSamples;
  u32 DecodedSampleIndex;
  f32 DecodedSamples[256];

  struct playing_sound* Next;
} playing_sound;

typedef struct audio_player {
  memory_arena AudioArena;

  v2 MasterVolume;
  playing_sound *FirstPlayingSound;
  playing_sound *FirstFreeSound;
} audio_player;

internal void AudioPlayerInit(audio_player *Player, memory_arena *PermanentArena);
internal void AudioPlayerDestroy(audio_player *Player);
internal void AudioPlayerStopAll(audio_player *Player, f32 FadeOutDurationSeconds);
internal void AudioPlayerStartAll(audio_player *Player, f32 FadeInDurationSeconds);
internal void PlayingSoundChangeVolume(playing_sound *Sound, v2 TargetVolume, f32 FadeDurationSeconds);
internal void PlayingSoundChangeLooping(playing_sound *Sound, b32 Loop);
internal playing_sound* AudioPlayerPlaySound(audio_player *Player, sound Sound, v2 StartVolume, b32 Loop);

internal void UpdateAndMixAudio(audio_player *Player, audio_buffer *AudioBuffer, f32 DeltaTimeSecs);

#endif // GAME_MIXER_H
