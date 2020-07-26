#include "textures.h"

typedef struct reload_texture_work {
  texture_catalog *TextureCatalog;
  char FileName[256];
  u32 EntryIndex;
} reload_texture_work;

void ReloadTextureCallback(work_queue *Queue, void *Data)
{
  reload_texture_work *Work = (reload_texture_work*)Data;
  
  i32 Width, Height, Channels;
  u8 *ImageData = stbi_load(Work->FileName, &Width, &Height, &Channels, STBI_rgb_alpha);
  if (ImageData != NULL)
  {
    texture_catalog_entry *Entry = Work->TextureCatalog->Entry + Work->EntryIndex;
    
    Entry->Texture.Dim = V2(Width, Height);
    glBindTexture(GL_TEXTURE_2D, Entry->Texture.ID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ImageData);
    glBindTexture(GL_TEXTURE_2D, 0);
    Entry->Texture.Loaded = true;
    
    fprintf(stderr, "hot reload: texture '%s'\n", Work->FileName);
    stbi_image_free(ImageData);
  }
  else 
  {
    fprintf(stderr, "error: failed to reload file: '%s'\n", Work->FileName);
  }
  
  glFinish();
  free(Work);
}

typedef struct load_texture_work {
  texture_catalog *TextureCatalog;
  char *ReferenceName;
} load_texture_work;

void LoadTextureCallback(work_queue *Queue, void *Data)
{
  load_texture_work *Work = (load_texture_work*)Data;
  
  if (strncmp(Work->ReferenceName, "monk_idle", TEXTURE_CATALOG_REFERENCE_NAME_MAX_SIZE) == 0)
  {
    TextureCatalogAdd(Work->TextureCatalog, "../assets/textures/MonkIdle.png", "monk_idle");
  }
  
  else if (strncmp(Work->ReferenceName, "guy_idle", TEXTURE_CATALOG_REFERENCE_NAME_MAX_SIZE) == 0)
  {
    TextureCatalogAdd(Work->TextureCatalog, "../assets/textures/GuyIdle.png", "guy_idle");
  }
  
  // NOTE: To avoid texture corruption and other problems related to a
  // separate thread uploading assets we call glFinish(). This ensures that
  // all threaded state changes are completed before the thread finishes
  // execution.
  glFinish();
  
  free(Work);
}

internal b32 TextureCatalogInit(texture_catalog *Catalog)
{
  Catalog->NumEntries = 0;
  Catalog->AllowAsync = false;
  return WatchedFileSetCreate(&Catalog->Watcher);
}

internal void TextureCatalogDestroy(texture_catalog *Catalog)
{
  foreach (I, Catalog->NumEntries)
  {
    glDeleteTextures(1, &Catalog->Entry[I].Texture.ID);
  }
}

internal b32 TextureCatalogAdd(texture_catalog *Catalog, char *TextureFile, char *ReferenceName)
{
  b32 Result = true;
  
  i32 Width, Height, Channels;
  u8 *ImageData = stbi_load(TextureFile, &Width, &Height, &Channels, STBI_rgb_alpha);
  if (ImageData != NULL)
  {
    u32 OldNextEntry = Catalog->NumEntries;
    u32 NewNextEntry = OldNextEntry + 1;
    Assert(OldNextEntry < TEXTURE_CATALOG_MAX_TEXTURES);
    
    u32 NextEntry = AtomicCompareAndExchangeU32(&Catalog->NumEntries, NewNextEntry, OldNextEntry);
    if (NextEntry == OldNextEntry)
    {
      texture_catalog_entry *Entry = Catalog->Entry + NextEntry;
      strncpy(Entry->ReferenceName, ReferenceName, TEXTURE_CATALOG_REFERENCE_NAME_MAX_SIZE);
      Entry->Texture.Loaded = true;
      Entry->Texture.Dim = V2(Width, Height);
      Entry->WatcherHandle = WatchedFileSetAdd(&Catalog->Watcher, TextureFile);
      
      glGenTextures(1, &Entry->Texture.ID);
      glBindTexture(GL_TEXTURE_2D, Entry->Texture.ID);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ImageData);
      glBindTexture(GL_TEXTURE_2D, 0);
      
      fprintf(stderr, "info: successfully loaded texture: %s: %d\n", TextureFile, Entry->Texture.ID);
    }
    
    stbi_image_free(ImageData);
  }
  else
  {
    fprintf(stderr, "error: failed to load texture %s\n", TextureFile);
    Result = false;
  }
  
  return(Result);
}

internal texture TextureCatalogGet(texture_catalog *Catalog, platform_state *Platform, char *ReferenceName)
{
  texture Result = {};
  foreach(I, Catalog->NumEntries)
  {
    texture_catalog_entry *Entry = Catalog->Entry + I;
    if (strncmp(Entry->ReferenceName, ReferenceName, TEXTURE_CATALOG_REFERENCE_NAME_MAX_SIZE) == 0)
    {
      Result = Entry->Texture;
      break;
    }
  }
  
  if (!Result.Loaded && Catalog->AllowAsync)
  {
    load_texture_work *Work = (load_texture_work*)calloc(1, sizeof(load_texture_work));
    Work->TextureCatalog = Catalog;
    Work->ReferenceName = ReferenceName;
    Platform->Interface.WorkQueueAddEntry(Platform->Input.WorkQueue, LoadTextureCallback, (void*)Work);
  }
  
  return(Result);
}

internal b32 TextureCatalogUpdate(texture_catalog *Catalog, platform_state *Platform)
{
  b32 Result = false;
  
  watched_file_iter Iter = WatchedFileSetUpdate(&Catalog->Watcher);
  while (Iter.IsValid)
  {
    foreach(I, Catalog->NumEntries)
    {
      texture_catalog_entry *Entry = Catalog->Entry + I;
      if (Entry->WatcherHandle == Iter.WatcherHandle)
      {
        // Mark texture as unloaded in preparation for reload.
        Entry->Texture.Loaded = false;
        
        // Queue a task to reload it.
        reload_texture_work *Work = (reload_texture_work*)calloc(1, sizeof(reload_texture_work));
        Work->TextureCatalog = Catalog;
        Work->EntryIndex = I;
        strncpy(Work->FileName, Iter.FileName, 256);
        
        Platform->Interface.WorkQueueAddEntry(Platform->Input.WorkQueue, ReloadTextureCallback, (void*)Work);
      }
    }
    
    Iter = WatchedFileIterNext(&Catalog->Watcher, Iter);
  }
  
  return(Result);
}
