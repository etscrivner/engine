#include "mixer.h"

#define ADJUST_VOLUME(Sample, Volume) (((Sample)*(Volume))/AUDIO_MAX_VOLUME)

internal void AudioPlayerStopAll(audio_player* Player, f32 FadeOutDurationSeconds)
{
  if (Player->FirstPlayingSound == NULL)
  {
    return;
  }

  playing_sound* Voice = Player->FirstPlayingSound;
  while (Voice)
  {
    {
      Voice->SavedVolume = Voice->CurrentVolume;
    }

    PlayingSoundChangeVolume(Voice, V2(0.0f, 0.0f), FadeOutDurationSeconds);
    Voice = Voice->Next;
  }
}

internal void AudioPlayerStartAll(audio_player* Player, f32 FadeInDurationSeconds)
{
  if (Player->FirstPlayingSound == NULL)
  {
    return;
  }

  playing_sound* Voice = Player->FirstPlayingSound;
  while (Voice != NULL)
  {
    PlayingSoundChangeVolume(Voice, Voice->SavedVolume, FadeInDurationSeconds);
    Voice = Voice->Next;
  }
}

internal void PlayingSoundChangeVolume(playing_sound* Sound, v2 TargetVolume, f32 FadeDurationSeconds)
{
  if (!Sound->IsPlaying)
  {
    return;
  }

  if (FadeDurationSeconds <= 0.0f)
  {
    // Instantly change the volume of the sound
    Sound->CurrentVolume = TargetVolume;
    Sound->TargetVolume = TargetVolume;
    Sound->dCurrentVolume = V2(0.0f, 0.0f);
  }
  else
  {
    Sound->TargetVolume = TargetVolume;
    f32 OneOverFade = 1.0f / FadeDurationSeconds;
    Sound->dCurrentVolume = OneOverFade * (Sound->TargetVolume - Sound->CurrentVolume);
  }
}

internal void PlayingSoundChangeLooping(playing_sound* Sound, b32 Loop)
{
  if (!Sound->IsPlaying)
  {
    return;
  }

  Sound->Loop = Loop;
}

internal b32 PlayingSoundCompleted(playing_sound* PlayingSound)
{
  b32 Result = false;

  if (stb_vorbis_get_sample_offset(PlayingSound->Decoder) >= (i32)PlayingSound->Sound.Samples &&
      PlayingSound->DecodedSampleIndex >= PlayingSound->NumDecodedSamples)
  {
    Result = true;
  }

  return(Result);
}

// TODO: Find a way to call this fewer times per frame and retrieve more
// samples at once.
internal void GetNextDecodedSamples(playing_sound* PlayingSound, f32* Samples)
{
  if (PlayingSound->DecodedSampleIndex < PlayingSound->NumDecodedSamples)
  {
    Assert((PlayingSound->NumDecodedSamples - PlayingSound->DecodedSampleIndex) >= PlayingSound->Sound.Channels);
    Samples[0] = PlayingSound->DecodedSamples[PlayingSound->DecodedSampleIndex++];
    if (PlayingSound->Sound.Channels >= 2) {
      Samples[1] = PlayingSound->DecodedSamples[PlayingSound->DecodedSampleIndex++];
    } else {
      Samples[1] = 0;
    }
  }
  else
  {
    if (!PlayingSoundCompleted(PlayingSound))
    {
      PlayingSound->NumDecodedSamples = (
        PlayingSound->Sound.Channels * stb_vorbis_get_samples_float_interleaved(
          PlayingSound->Decoder,
          PlayingSound->Sound.Channels,
          PlayingSound->DecodedSamples,
          ArrayCount(PlayingSound->DecodedSamples)
        )
      );
      PlayingSound->DecodedSampleIndex = 0;
      Samples[0] = PlayingSound->DecodedSamples[PlayingSound->DecodedSampleIndex++];
      if (PlayingSound->Sound.Channels >= 2) {
        Samples[1] = PlayingSound->DecodedSamples[PlayingSound->DecodedSampleIndex++];
      } else {
        Samples[1] = 0;
      }
    }
  }
}

internal playing_sound* AudioPlayerPlaySound(audio_player* Player, sound Sound, v2 StartVolume, b32 Loop)
{
  playing_sound* Result = Player->FirstFreeSound;

  if (Result == NULL)
  {
    Result = ArenaPushStruct(&Player->AudioArena, playing_sound);
  }
  else
  {
    // First free sound is no longer free, so move to the next free sound
    Player->FirstFreeSound = Result->Next;
  }

  Result->Sound = Sound;

  int Error;
  Result->Decoder = stb_vorbis_open_memory(Sound.Data, Sound.DataLength, &Error, NULL);
  Assert(Result->Decoder != NULL);

  Result->IsPlaying = true;
  Result->CurrentVolume = StartVolume;
  Result->TargetVolume = Result->CurrentVolume;
  Result->dCurrentVolume = V2(0.0f, 0.0f);
  Result->Loop = Loop;
  Result->Next = NULL;

  Result->NumDecodedSamples = 0;
  Result->DecodedSampleIndex = 0;

  Result->NumDecodedSamples = (
    Result->Sound.Channels *
    stb_vorbis_get_samples_float_interleaved(
      Result->Decoder, Result->Sound.Channels, Result->DecodedSamples, ArrayCount(Result->DecodedSamples)));
  Result->DecodedSampleIndex = 0;

  if (Player->FirstPlayingSound != NULL)
  {
    Result->Next = Player->FirstPlayingSound;
  }

  Player->FirstPlayingSound = Result;
  return(Result);
}

internal void DestroyPlayingSound(playing_sound* PlayingSound)
{
  stb_vorbis_close(PlayingSound->Decoder);
  PlayingSound->Decoder = NULL;
}

internal void RestartPlayingSound(playing_sound* PlayingSound)
{
  stb_vorbis_seek_start(PlayingSound->Decoder);
}

// TODO: This mixer could use another performance pass. The idea would be to
// stop using a linked list for channels and instead use a statically sized
// array (we probably should have been using one in the first place). This will
// probably be more cache-efficient than the linked list and provide some level
// of performance (unknown how much of a boost).
internal void MixAudio(audio_player* Player, i16* OutSamples, u32 FramesToPlay, u32 SamplesPerSecond)
{
  // NOTE: We need to clear the audio stream to ensure that if no voices are
  // playing only silence is emitted.
  memset(OutSamples, 0, FramesToPlay*sizeof(i16)*2);

  if (Player->FirstPlayingSound == NULL)
  {
    return;
  }

  f32 SecondsPerSample = 1.0f / (f32)SamplesPerSecond;

  playing_sound* Voice = Player->FirstPlayingSound;
  playing_sound* Prev = NULL;

  f32 Samples[2] = { 0, 0 };
  f32 TempSamples[2] = { 0, 0 };

  while (Voice != NULL)
  {
    i16 *Output = OutSamples;

    // Early out for sounds that have not yet loaded
    if (!Voice->Sound.Loaded)
    {
      goto finish_voice;
    }

    for (u32 J = 0; J < FramesToPlay; ++J)
    {
      GetNextDecodedSamples(Voice, TempSamples);

      for (u32 Channel = 0; Channel < 2; ++Channel)
      {
        Samples[Channel] = Voice->CurrentVolume.E[Channel] * TempSamples[Channel];

        if (Voice->dCurrentVolume.E[Channel] > 0 && Voice->CurrentVolume.E[Channel] >= Voice->TargetVolume.E[Channel])
        {
          Voice->CurrentVolume.E[Channel] = Voice->TargetVolume.E[Channel];
          Voice->dCurrentVolume.E[Channel] = 0.0f;
        }

        if (Voice->dCurrentVolume.E[Channel] < 0 && Voice->CurrentVolume.E[Channel] <= Voice->TargetVolume.E[Channel])
        {
          Voice->CurrentVolume.E[Channel] = Voice->TargetVolume.E[Channel];
          Voice->dCurrentVolume.E[Channel] = 0.0f;
        }
      }

      Voice->CurrentVolume += SecondsPerSample * Voice->dCurrentVolume;

      // TODO: Unroll this?
      for (u32 I = 0; I < 2; ++I)
      {
        FASTDEF(temp);
        int v = Player->MasterVolume.E[I] * FAST_SCALED_FLOAT_TO_INT(temp, Samples[I], 15);
        if ((unsigned int) (v + 32768) > 65535)
          v = v < 0 ? -32768 : 32767;
        *Output = Clamp(*Output + (i16)v, -32768, 32767);
        Output++;
      }

      if (PlayingSoundCompleted(Voice))
      {
        if (Voice->Loop)
        {
          RestartPlayingSound(Voice);
        }
        else
        {
          Voice->IsPlaying = false;
          DestroyPlayingSound(Voice);

          if (Prev)
          {
            Prev->Next = Voice->Next;
          }
          else
          {
            Player->FirstPlayingSound = Voice->Next;
          }

          if (Player->FirstFreeSound)
          {
            playing_sound* FreeVoice = Voice;
            Voice = Voice->Next;
            FreeVoice->Next = Player->FirstFreeSound;
            Player->FirstFreeSound = FreeVoice;
          }
          else
          {
            Player->FirstFreeSound = Voice;
            Voice = Voice->Next;
            Player->FirstFreeSound->Next = NULL;
          }

          // TODO: This is a bit janky... we can probably do this without using
          // a goto
          goto channel_finished_loopend;
        }
      }
    }

  finish_voice:
    Prev = Voice;
    Voice = Voice->Next;
  channel_finished_loopend:
    UNUSED(Voice);
  }
}

internal void UpdateAndMixAudio(audio_player* Player, audio_buffer* AudioBuffer, f32 DeltaTimeSecs)
{
  MixAudio(Player, AudioBuffer->Samples, AudioBuffer->FrameCount, AudioBuffer->SamplesPerSecond);
}

internal void AudioPlayerInit(audio_player* Player, memory_arena *PermanentArena)
{
  Player->AudioArena = ArenaPushChild(PermanentArena, AUDIO_PLAYER_ARENA_SIZE);

  Player->MasterVolume = V2(1.0f, 1.0f);
  Player->FirstPlayingSound = NULL;
  Player->FirstFreeSound = NULL;
}

internal void AudioPlayerDestroy(audio_player* Player)
{
  playing_sound* Sound = Player->FirstPlayingSound;

  while (Sound)
  {
    DestroyPlayingSound(Sound);
    Sound = Sound->Next;
  }
}
