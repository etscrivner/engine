#include "sounds.h"

internal b32 SoundManagerInit(sound_manager *SoundManager, const char *SoundDirectory)
{
  SoundManager->SoundDirectory = SoundDirectory;
  return(true);
}

internal void SoundManagerDestroy(sound_manager *SoundManager)
{
}

internal b32 SoundManagerLoadSound(sound_manager *SoundManager, sound *Sound, platform_state *Platform, const char *SoundFile)
{
  local_persist char SoundFilePath[256];
  b32 Result = false;

  snprintf(SoundFilePath, 256, "%s/%s", SoundManager->SoundDirectory, SoundFile);

  platform_entire_file File;
  if (Platform->Interface.LoadEntireFile(SoundFilePath, &File))
  {
    // Sanity check audio by attempting to decode the data
    int  Error;
    stb_vorbis *Vorbis = stb_vorbis_open_memory(File.Data, File.SizeBytes, &Error, NULL);
    if (Vorbis)
    {
      // Get sound info
      stb_vorbis_info SoundInfo = stb_vorbis_get_info(Vorbis);

      // Copy the file data into platform memory
      Sound->Loaded = true;
      Sound->SoundFile = File;
      Sound->Channels = SoundInfo.channels;
      Sound->SampleRate = SoundInfo.sample_rate;
      Sound->Samples = stb_vorbis_stream_length_in_samples(Vorbis);
      Sound->DataLength = File.SizeBytes;
      Sound->Data = File.Data;

      printf("Audio: Loaded sound\n");
      printf("\t%s\n", SoundFile);
      printf("\tChannels: %d\n", Sound->Channels);
      printf("\tSampleRate: %d\n", Sound->SampleRate);
      printf("\tSamples: %d\n", Sound->Samples);

      Result = true;

      stb_vorbis_close(Vorbis);
    }
    else
    {
      fprintf(stderr, "error: unable to decode audio file '%s': stb_vorbis error code %d\n", SoundFilePath, Error);
    }
  }
  else
  {
    fprintf(stderr, "error: unable to open sound file '%s'\n", SoundFilePath);
  }

  return(Result);
}

internal void SoundManagerDestroySound(sound_manager *SoundManager, sound *Sound, platform_state *Platform)
{
  // Close the file
  Platform->Interface.FreeEntireFile(&Sound->SoundFile);

  Sound->Loaded = false;
  Sound->Data = NULL;
  Sound->DataLength = 0;
}
