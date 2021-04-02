/*
 Implements a set for watching multiple files for modifications at once.

To begin with you can create a new watched file set and add items to it. The
basic life-cycle for a watched file set is as follows:

  watched_file_set ShaderWatcher;
  WatchedFileSetCreate(&ShaderWatcher);
  WatchedFileSetAdd(&ShaderWatcher, "./assets/shaders/tone_mapper.gl");
  WatchedFileSetAdd(&ShaderWatcher, "./assets/shaders/bitmap_font.gl");
  WatchedFileSetDestroy(&ShaderWatcher);

All polling for file updates is non-blocking. Each loop through your
application you can get the pathnames for all files that have pending updates
as follows:

    watched_file_iter Iter = WatchedFileSetUpdate(&ShaderWatcher);
    while (IsValid(Iter)) {
      // ... Handle file update in some way ...
      Iter = WatchedFileIterNext(&ShaderWatcher, Iter);
    }

*/
#ifndef WATCHED_FILE_SET_H
#define WATCHED_FILE_SET_H

#include <unistd.h>
#include <sys/inotify.h>

#include "common/language_layer.h"

#define WATCHED_FILE_SET_MAX_SIZE 256
#define WATCHED_FILE_NAME_MAX_LENGTH 256
#define WATCHED_FILE_SET_ITER_BUF_SIZE 1024

typedef struct watched_file_set {
  u32 NumItems;
  i32 WatchDescriptors[WATCHED_FILE_SET_MAX_SIZE];
  // NOTE: We copy file names into here to avoid issues that might occur with
  // library hot-reloading when in-memory string addresses change between 
  // versions of a library.
  char FileNames[WATCHED_FILE_SET_MAX_SIZE][WATCHED_FILE_NAME_MAX_LENGTH];
  i32 InotifyHandle;
  char IterBuf[WATCHED_FILE_SET_ITER_BUF_SIZE] __attribute__ ((aligned(8)));
} watched_file_set;

typedef struct watched_file_iter {
  b32 IsValid;
  char *FileName;
  i32 WatcherHandle;
  
  struct {
    struct inotify_event *Event;
    char *Ptr;
    char *EndPtr;
  } Internal;
} watched_file_iter;

internal b32 WatchedFileSetCreate(watched_file_set *Set);
internal void WatchedFileSetDestroy(watched_file_set *Set);

// Returns the watched file handle which can be used to later remove or reference
// the given watched file.
internal i32 WatchedFileSetAdd(watched_file_set *Set, char *FileName);
internal b32 WatchedFileSetRemove(watched_file_set *Set, i32 WatcherHandle);
internal b32 WatchedFileSetRemoveByFile(watched_file_set *Set, char *FileName);

internal watched_file_iter WatchedFileSetUpdate(watched_file_set *Set);
internal b32 WatchedFileIterIsValid(watched_file_iter Iter);
internal watched_file_iter WatchedFileIterNext(watched_file_set *Set, watched_file_iter Iter);

#ifdef WATCHED_FILE_SET_IMPLEMENTATION

internal b32 WatchedFileSetCreate(watched_file_set *Set)
{
  b32 Result = true;
  
  *Set = {};
  Set->NumItems = 0;
  Set->InotifyHandle = inotify_init1(IN_NONBLOCK);
  if (Set->InotifyHandle == -1) {
    fprintf(stderr, "error: unable to initialize inoitfy: %s\n", strerror(errno));
    Result = false;
  }
  
  return(Result);
}

internal void WatchedFileSetDestroy(watched_file_set *Set)
{
  for (u32 I = 0; I < Set->NumItems; ++I)
  {
    i32 Result = inotify_rm_watch(Set->InotifyHandle, Set->WatchDescriptors[I]);
    if (Result == -1) {
      fprintf(stderr, "error: failed to remove watch: %s\n", strerror(errno));
    }
  }
  
  close(Set->InotifyHandle);
}

internal i32 WatchedFileSetAdd(watched_file_set *Set, char *FileName)
{
  i32 Result = -1;
  
  u32 OldNextEntry = Set->NumItems;
  u32 NewNextEntry = OldNextEntry + 1;
  
  Assert(OldNextEntry < WATCHED_FILE_SET_MAX_SIZE);
  
  u32 NextEntry = AtomicCompareAndExchangeU32(&Set->NumItems, NewNextEntry, OldNextEntry);
  if (NextEntry == OldNextEntry) {
    // NOTE: Only monitor for IN_CLOSE_WRITE as this indicates that all pending
    // writes have completed and been safely persisted to the file. This is 
    // also much less noisy than watching for IN_MODIFY which happens whenever
    // a write is done to a file (so could happen multiple times for a file that
    // is being update.
    Set->WatchDescriptors[NextEntry] = inotify_add_watch(Set->InotifyHandle, FileName, IN_CLOSE_WRITE);
    if (Set->WatchDescriptors[NextEntry] != -1) {
      Assert(strlen(FileName) < WATCHED_FILE_NAME_MAX_LENGTH);
      Result = Set->WatchDescriptors[NextEntry];
      strncpy(Set->FileNames[NextEntry], FileName, WATCHED_FILE_NAME_MAX_LENGTH);
      // fprintf(stderr, "watched file added: %s\n", Set->FileNames[NextEntry]);
    } else {
      fprintf(stderr, "error: unable to add file '%s' to set: %s\n", FileName, strerror(errno));
    }
  }
  
  return(Result);
}

static b32 WatchedFileSetRemove(watched_file_set *Set, i32 WatcherHandle)
{
  b32 Result = false;
  i32 ItemIndex = -1;
  for (u32 I = 0; I < Set->NumItems; ++I)
  {
    if (Set->WatchDescriptors[I] == WatcherHandle)
    {
      ItemIndex = (i32)I;
      break;
    }
  }
  
  if (ItemIndex != -1)
  {
    Result = true;
    if (inotify_rm_watch(Set->InotifyHandle, Set->WatchDescriptors[ItemIndex]) == -1) {
      fprintf(stderr, "error: failed to remove watched for file %s: %s\n", Set->FileNames[ItemIndex], strerror(errno));
    }
    
    // Remove the item from the watched arrays
    for (u32 I = (u32)ItemIndex; I < Set->NumItems; ++I) {
      Set->WatchDescriptors[I] = Set->WatchDescriptors[I+1];
      strncpy(Set->FileNames[I], Set->FileNames[I+1], WATCHED_FILE_NAME_MAX_LENGTH);
    }
    
    --Set->NumItems;
  }
  
  return(Result);
}

// TODO: This is _NOT_ thread-safe, and probably cannot be safely used with
// threaded additions to watched_file_set. We would need to introduce a mutex
// of some sort to allow only one thread to modify the state of the set at any
// given time.
static b32 WatchedFileSetRemoveByFile(watched_file_set *Set, char *FileName)
{
  b32 Result = false;
  i32 ItemIndex = -1;
  for (u32 I = 0; I < Set->NumItems; ++I)
  {
    if (strncmp(Set->FileNames[I], FileName, WATCHED_FILE_NAME_MAX_LENGTH) == 0)
    {
      ItemIndex = (i32)I;
      break;
    }
  }
  
  if (ItemIndex != -1)
  {
    Result = true;
    if (inotify_rm_watch(Set->InotifyHandle, Set->WatchDescriptors[ItemIndex]) == -1) {
      fprintf(stderr, "error: failed to remove watched for file %s: %s\n", Set->FileNames[ItemIndex], strerror(errno));
    }
    
    // Remove the item from the watched arrays
    for (u32 I = (u32)ItemIndex; I < Set->NumItems; ++I) {
      Set->WatchDescriptors[I] = Set->WatchDescriptors[I+1];
      strncpy(Set->FileNames[I], Set->FileNames[I+1], WATCHED_FILE_NAME_MAX_LENGTH);
    }
    
    --Set->NumItems;
  }
  
  return(Result);
}

static watched_file_iter WatchedFileSetUpdate(watched_file_set *Set) {
  watched_file_iter Result = {};
  Result.IsValid = 0;
  Result.FileName = NULL;
  Result.Internal.Event = NULL;
  Result.Internal.Ptr = NULL;
  Result.Internal.EndPtr = NULL;
  
  int NumRead = read(Set->InotifyHandle, Set->IterBuf, WATCHED_FILE_SET_ITER_BUF_SIZE);
  if (NumRead == 0) {
    fprintf(stderr, "error: failed to read from inotify fd: %s\n", strerror(errno));
  } else if (NumRead == -1) {
    // NOTE: Do nothing on EAGAIN as this just indicates non-blocking IO had no
    // updates for us on this read.
    if (errno != EAGAIN) {
      fprintf(stderr, "error: read failed with error: %s\n", strerror(errno));
    }
  } else {
    Result.IsValid = 0;
    Result.FileName = NULL;
    Result.Internal.Ptr = Set->IterBuf;
    Result.Internal.EndPtr = Set->IterBuf + NumRead;
    
    while (Result.Internal.Ptr < Result.Internal.EndPtr) {
      Result.Internal.Event = (struct inotify_event *)Result.Internal.Ptr;
      if ((Result.Internal.Event->mask & IN_CLOSE_WRITE)) {
        Result.IsValid = 1;
        break;
      }
      
      Result.Internal.Ptr += sizeof(struct inotify_event) + Result.Internal.Event->len;
    }
    
    if (Result.IsValid) {
      foreach(I, Set->NumItems) {
        if (Set->WatchDescriptors[I] == Result.Internal.Event->wd) {
          Result.FileName = Set->FileNames[I];
          Result.WatcherHandle = Result.Internal.Event->wd;
          break;
        }
      }
    }
  }
  
  return(Result);
}

static b32 IsValid(watched_file_iter Iter) {
  return(Iter.IsValid && Iter.FileName != NULL);
}

static watched_file_iter WatchedFileIterNext(watched_file_set *Set, watched_file_iter Iter) {
  watched_file_iter Result = {};
  Result.IsValid = 0;
  Result.FileName = NULL;
  Result.Internal.Ptr = Iter.Internal.Ptr;
  Result.Internal.EndPtr = Iter.Internal.EndPtr;
  
  Result.Internal.Ptr += sizeof(struct inotify_event) + Iter.Internal.Event->len;
  
  while (Result.Internal.Ptr < Result.Internal.EndPtr) {
    Result.Internal.Event = (struct inotify_event *)Result.Internal.Ptr;
    if ((Result.Internal.Event->mask & IN_CLOSE_WRITE)) {
      Result.IsValid = 1;
      break;
    }
    
    Result.Internal.Ptr += sizeof(struct inotify_event) + Result.Internal.Event->len;
  }
  
  if (Result.IsValid) {
    foreach(I, Set->NumItems) {
      if (Set->WatchDescriptors[I] == Result.Internal.Event->wd) {
        Result.FileName = Set->FileNames[I];
        Result.WatcherHandle = Result.Internal.Event->wd;
        break;
      }
    }
  }
  
  return(Result);
}

#endif // WATCHED_FILE_SET_IMPLEMENTATION

#endif //WATCHED_FILE_SET_H
