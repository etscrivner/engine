#ifndef WATCHED_FILE_H
#define WATCHED_FILE_H

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct watched_file {
  const char *FilePath;
  
#if defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)
  ino_t InodeID;
  struct timespec LastModifiedTime;
#elif defined(PLATFORM_WIN)
#error watched_file: Windows not implemented yet
#else
#error watched_file: Not supported on this platform
#endif
  
  struct {
    int ReturnValue;
    int Errno;
  } Error;
  
  bool WasModified;
} watched_file;

watched_file WatchedFile(const char *FilePath)
{
  struct stat Attr;
  watched_file Result = {
    .FilePath          = FilePath,
    .Error.ReturnValue = stat(FilePath, &Attr),
    .WasModified       = false
  };
  
  if (Result.Error.ReturnValue == 0)
  {
    Result.InodeID = Attr.st_ino;
    Result.LastModifiedTime = Attr.st_mtim;
  }
  else
  {
    Result.Error.Errno = errno;
  }
  
  return(Result);
}

bool IsValid(watched_file *WatchedFile)
{
  return(WatchedFile->FilePath != NULL);
}

bool WatchedFileHasError(watched_file *WatchedFile)
{
  return(WatchedFile->Error.ReturnValue != 0);
}

char* WatchedFileGetError(watched_file *WatchedFile)
{
  return(strerror(WatchedFile->Error.Errno));
}

void WatchedFileUpdate(watched_file *WatchedFile)
{
  WatchedFile->WasModified = false;
  
  struct stat Attr;
  WatchedFile->Error.ReturnValue = stat(WatchedFile->FilePath, &Attr);
  if (WatchedFile->Error.ReturnValue != 0)
  {
    WatchedFile->Error.Errno = errno;
    return;
  }
  
  if (WatchedFile->InodeID != Attr.st_ino || WatchedFile->LastModifiedTime.tv_nsec != Attr.st_mtim.tv_nsec)
  {
    WatchedFile->InodeID = Attr.st_ino;
    WatchedFile->LastModifiedTime = Attr.st_mtim;
    WatchedFile->WasModified = true;
  }
}

#endif // WATCHED_FILE_H
