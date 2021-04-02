#define ALSA_PCM_NEW_HW_PARAMS_API // Use the new ALSA API
#include <alsa/asoundlib.h>

#define AUDIO_DEFAULT_CHANNELS 2
#define AUDIO_DEFAULT_DEVICE_NAME "default"

// Represents our circular audio buffer queue. Stores data from the game on its
// way to the audio card for the audio thread to later read.
typedef struct circular_audio_buffer {
  i16 *Samples;
  u32 SampleCount;
  u32 ReadCursor;
  u32 WriteCursor;
} circular_audio_buffer;

internal void CircularAudioBufferCreate(circular_audio_buffer *CircularBuffer, u32 SampleCount);
internal void CircularAudioBufferDestroy(circular_audio_buffer *CircularBuffer);
internal u32 CircularAudioBufferFramesToWrite(circular_audio_buffer *CircularBuffer);
internal void CircularAudioBufferWrite(circular_audio_buffer *CircularBuffer, i16 *Samples, u32 SampleCount);
internal void CircularAudioBufferRead(circular_audio_buffer *CircularBuffer, i16 *Samples, u32 SampleCount);

internal void CircularAudioBufferCreate(circular_audio_buffer *CircularBuffer, u32 SampleCount)
{
  printf("\tCircular buffer size: %d samples\n", SampleCount);
  CircularBuffer->Samples = (i16 *)calloc(1, SampleCount * sizeof(i16));
  CircularBuffer->SampleCount = SampleCount;
  CircularBuffer->ReadCursor = 0;
  CircularBuffer->WriteCursor = 0;
}

internal void CircularAudioBufferDestroy(circular_audio_buffer *CircularBuffer)
{
  free(CircularBuffer->Samples);
}

internal u32 CircularAudioBufferFramesToWrite(circular_audio_buffer *CircularBuffer)
{
  u32 ReadCursor = CircularBuffer->ReadCursor;
  u32 SamplesToWrite = 0;
  if (ReadCursor < CircularBuffer->WriteCursor)
  {
    // NOTE: Go right up to the read cursor but do not overwrite it in order to
    // prevent overrun and audio pops.
    SamplesToWrite = (CircularBuffer->SampleCount - CircularBuffer->WriteCursor) + (ReadCursor - 1);
  }
  else
  {
    SamplesToWrite = (ReadCursor - CircularBuffer->WriteCursor);
  }

  // NOTE: We divide by 4 and then pick the next power of 2 here in order to
  // prevent overflowing things when a large audio write is required by
  // requesting more samples than the platform-layer buffer can hold.
  return(NextPowerOf2(SamplesToWrite >> 2));
}

internal void CircularAudioBufferWrite(circular_audio_buffer *CircularBuffer, i16 *SamplesIn, u32 SampleCount)
{
  // @Performance: Replace this with something more efficient. Possible using
  // memcpy.
  foreach(I, SampleCount)
  {
    CircularBuffer->Samples[CircularBuffer->WriteCursor++] = *SamplesIn++;
    if (CircularBuffer->WriteCursor == CircularBuffer->ReadCursor)
    {
      // Overrun
      // Assert(CircularBuffer->WriteCursor != CircularBuffer->ReadCursor);
    }

    if (CircularBuffer->WriteCursor >= CircularBuffer->SampleCount)
    {
      CircularBuffer->WriteCursor = 0;
    }
  }
}

internal void CircularAudioBufferRead(circular_audio_buffer *CircularBuffer, i16 *SamplesOut, u32 SampleCount)
{
  foreach(I, SampleCount)
  {
    *SamplesOut++ = CircularBuffer->Samples[CircularBuffer->ReadCursor++];
    if (CircularBuffer->ReadCursor == CircularBuffer->WriteCursor)
    {
      // Underrun. Note than an underrun is expected right at audio thread
      // start but should not happen again in practice as we try to write enough
      // to handle any level of greed for samples from the audio thread.
    }

    if (CircularBuffer->ReadCursor >= CircularBuffer->SampleCount)
    {
      CircularBuffer->ReadCursor = 0;
    }
  }
}

typedef struct linux_audio {
  i32                Channels;
  u32                SamplesPerSecond;
  u32                BytesPerSample;
  f32                PeriodTimeMS;
  snd_pcm_uframes_t  BufferSize;
  snd_pcm_uframes_t  PeriodSize;
  i16*               SamplesIn;
  i16*               SamplesOut;
  snd_pcm_t         *Handle;
  b32                IsPlaying;
  b32                ExitThread;
  thread_ptr_t       Thread;
  circular_audio_buffer CircularBuffer;
} linux_audio;

internal b32 LinuxAudioCreate(linux_audio *Audio, i32 LatencyMS, u32 SamplesPerSecond);
internal void LinuxAudioDestroy(linux_audio *Audio);
internal void LinuxAudioStart(linux_audio *Audio);
internal u32 LinuxAudioFramesToWrite(linux_audio *Audio);
internal void LinuxAudioFill(linux_audio *Audio, i16 *Samples, u32 FrameCount);
internal i32 LinuxAudioThreadLoop(void *UserData);

internal b32 LinuxAudioCreate(linux_audio *Audio, i32 LatencyMS, u32 SamplesPerSecond)
{
  // TODO: Add 4 and 6 channel surround sound support
  Audio->Channels = AUDIO_DEFAULT_CHANNELS;
  Audio->SamplesPerSecond = SamplesPerSecond;
  Audio->BytesPerSample = sizeof(i16) * AUDIO_DEFAULT_CHANNELS;
  Audio->Handle = NULL;
  Audio->IsPlaying = false;
  Audio->ExitThread = false;

  printf("ALSA:\n");
  printf("\tVersion: %s\n", SND_LIB_VERSION_STR);
  printf("\tSampling Rate: %d samples / sec\n", SamplesPerSecond);

  i32 Result;
  snd_pcm_t *AudioHandle = NULL;
  snd_pcm_hw_params_t *HWParams;
  snd_pcm_sw_params_t *SWParams;

#define CHECK_ALSA_RESULT(AlsaResult)                                   \
  if ((AlsaResult) < 0) {                                               \
    fprintf(stderr, "Audio: ALSA error: %s", snd_strerror(AlsaResult)); \
    if (AudioHandle) {                                                  \
      snd_pcm_close(AudioHandle);                                       \
      AudioHandle = NULL;                                               \
    }                                                                   \
    return(false);                                                      \
  }

  // Attempt to open audio device handle
  Result = snd_pcm_open(&AudioHandle, AUDIO_DEFAULT_DEVICE_NAME, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  CHECK_ALSA_RESULT(Result);

  // Audio hardware parameter configuration
  snd_pcm_hw_params_alloca(&HWParams);

  Result = snd_pcm_hw_params_any(AudioHandle, HWParams);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_hw_params_set_access(AudioHandle, HWParams, SND_PCM_ACCESS_RW_INTERLEAVED);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_hw_params_set_format(AudioHandle, HWParams, SND_PCM_FORMAT_S16_LE);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_hw_params_set_channels(AudioHandle, HWParams, Audio->Channels);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_hw_params_set_rate_near(AudioHandle, HWParams, &Audio->SamplesPerSecond, 0);
  CHECK_ALSA_RESULT(Result);

  // Calculate the audio buffer size based on the desired audio latency in
  // milliseconds.
  u32 Periods = 2;
  snd_pcm_uframes_t BufferFrames = NextPowerOf2(LatencyMS * Audio->SamplesPerSecond / 1000);
  Audio->BufferSize = BufferFrames * Periods;
  Audio->PeriodSize = BufferFrames;

  Result = snd_pcm_hw_params_set_buffer_size_near(AudioHandle, HWParams, &Audio->BufferSize);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_hw_params_set_period_size_near(AudioHandle, HWParams, &Audio->PeriodSize, 0);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_hw_params_set_periods_near(AudioHandle, HWParams, &Periods, 0);
  CHECK_ALSA_RESULT(Result);

  printf(
    "\tBuffer: %lu bytes, %lu frames, %u periods, %0.02f ms calculated latency\n",
    Audio->BufferSize,
    Audio->PeriodSize,
    Periods,
    (Audio->PeriodSize * Periods * 1000) / (f32)(Audio->SamplesPerSecond * Audio->BytesPerSample)
  );

  Result = snd_pcm_hw_params(AudioHandle, HWParams);
  CHECK_ALSA_RESULT(Result);

  // Get period time in milliseconds
  u32 PeriodTimeMicroSecs = 0;
  Result = snd_pcm_hw_params_get_period_time(HWParams, &PeriodTimeMicroSecs, 0);
  CHECK_ALSA_RESULT(Result);

  Audio->PeriodTimeMS = PeriodTimeMicroSecs / 1000.0f;
  printf("\tPeriod Time: %0.02f ms\n", Audio->PeriodTimeMS);

  // Audio software parameters
  snd_pcm_sw_params_alloca(&SWParams);

  Result = snd_pcm_sw_params_current(AudioHandle, SWParams);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_sw_params_set_avail_min(AudioHandle, SWParams, Audio->PeriodSize);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_sw_params_set_start_threshold(AudioHandle, SWParams, 1);
  CHECK_ALSA_RESULT(Result);

  Result = snd_pcm_sw_params(AudioHandle, SWParams);
  CHECK_ALSA_RESULT(Result);

  // Allocate sample buffers
  Audio->SamplesIn = (i16 *)calloc(1, sizeof(i16) * Audio->PeriodSize * Audio->Channels);
  Audio->SamplesOut = (i16 *)calloc(1, sizeof(i16) * Audio->PeriodSize * Audio->Channels);

  Audio->Handle = AudioHandle;

  // NOTE: @Portability: The MaxBufferedPeriods size is very important for
  // preventing audio underruns. This was tuned by hand to produce good audio
  // with low latency at 60 FPS, may not port to different machines or
  // framerates.
  u32 MaxBufferedPeriods = Audio->BufferSize >> 2;
  CircularAudioBufferCreate(&Audio->CircularBuffer, MaxBufferedPeriods * Audio->PeriodSize * Audio->Channels);

  // Start audio thread
  printf("Audio: Thread: Starting\n");
  Audio->Thread = thread_create(LinuxAudioThreadLoop, Audio, THREAD_STACK_SIZE_DEFAULT);
  thread_set_high_priority(Audio->Thread);

  return(true);
}

internal void LinuxAudioDestroy(linux_audio *Audio)
{
  if (Audio->Thread)
  {
    Audio->ExitThread = true;
    i32 ReturnValue = thread_join(Audio->Thread);
    printf("Audio: Thread: Exit code %d\n", ReturnValue);
    thread_destroy(Audio->Thread);
  }

  if (Audio->Handle)
  {
    snd_pcm_drain(Audio->Handle);
    snd_pcm_close(Audio->Handle);
    Audio->Handle = NULL;
  }

  if (Audio->SamplesIn)
  {
    free(Audio->SamplesIn);
  }

  if (Audio->SamplesOut)
  {
    free(Audio->SamplesOut);
  }

  CircularAudioBufferDestroy(&Audio->CircularBuffer);
}

internal void LinuxAudioStart(linux_audio *Audio)
{
  Audio->IsPlaying = true;
}

internal void WriteSineWave(i16* Samples, i32 SamplesPerSecond, f32* Time, u32 FrameCount)
{
  i16 ToneVolume = 1000;
  i32 ToneHz = (250 + 0.5 * 150);
  i32 WavePeriod = SamplesPerSecond / ToneHz;
  foreach(I, FrameCount) {
    f32 SineValue = sinf(*Time);
    i16 Value = (i32)(SineValue * ToneVolume);
    if (Value < -32768)
      Value = -32768;
    if (Value > 32767)
      Value = 32767;
    *Samples++ = Value;
    *Samples++ = Value;
    *Time += TAU * (1.0f / (f32)WavePeriod);
    if (*Time >= TAU)
    {
      *Time -= TAU;
    }
  }
}

internal u32 LinuxAudioFramesToWrite(linux_audio *Audio)
{
  return(CircularAudioBufferFramesToWrite(&Audio->CircularBuffer));
}

internal void LinuxAudioFill(linux_audio *Audio, i16 *Samples, u32 FrameCount)
{
  CircularAudioBufferWrite(&Audio->CircularBuffer, Samples, FrameCount * 2);
}

internal i32 LinuxAudioThreadLoop(void *UserData)
{
  linux_audio *Audio = (linux_audio *)UserData;

  f32 Time = 0.0f;

  while (!Audio->ExitThread)
  {
    i16 *Samples = Audio->SamplesOut;

    if (!Audio->IsPlaying)
    {
      foreach(I, Audio->PeriodSize * Audio->Channels)
      {
        *Samples++ = 0;
      }
    }
    else
    {
#if 0
      WriteSineWave(Samples, Audio->SamplesPerSecond, &Time, Audio->PeriodSize);
#else
      CircularAudioBufferRead(&Audio->CircularBuffer, Audio->SamplesOut, Audio->PeriodSize * Audio->Channels);
#endif
    }

    i32 ToWrite = Audio->PeriodSize;
    i32 Total = 0;

    while (ToWrite && !Audio->ExitThread)
    {
      u8 *Src = (u8 *)Audio->SamplesOut;
      i32 Wrote = snd_pcm_writei(Audio->Handle, (void *)(Src + (Total * Audio->Channels)), ToWrite);

      if (Wrote > 0) {
        Total += Wrote;
        ToWrite -= Wrote;
      } else if (Wrote == -EAGAIN) {
        LinuxSleep(1);
      } else {
        // NOTE: I'm not sure of the benefits and trade-offs of each. Different
        // examples do things differently, but recover seems to work better in
        // the case of an underrun.
#if 0
        snd_pcm_prepare(Audio->Handle);
#else
        Wrote = snd_pcm_recover(Audio->Handle, Wrote, 0);
        if (Wrote < 0)
        {
          fprintf(stderr, "Audio: ALSA error: failed and cannot recover: %s\n", snd_strerror(Wrote));
          Audio->IsPlaying = false;
          Audio->ExitThread = true;
        }
#endif
      }
    }
  }

  return(0);
}
