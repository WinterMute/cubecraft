#include "global.h"
#include "file.h"
#include "world.h"

#define CHUNK_MOD_SIZE (4 * sizeof(uint32_t))

typedef unsigned char byte;

//TODO: Use actual error handling rather than assert

static const char fileMagic[] = "CUBECRAFTvALPHA";
static const char savePath[] = "/apps/cubecraft/worlds";
static const char logFilePath[] = "/apps/cubecraft/log.txt";

static bool can_open_root_dir(void)
{
    DIR *rootDir = opendir("/");
    if (rootDir != NULL)
    {
        closedir(rootDir);
        return true;
    }
    else
    {
        return false;
    }
}

static void ensure_worlds_dir(void)
{
    DIR *saveDir = opendir(savePath);
    
    if (saveDir != NULL)
    {
        closedir(saveDir);
    }
    else
    {
        //Directory doesn't exist. Create it.
        mkdir(savePath, 0777); //This 0777 is in octal
        
        //Make sure we can open it
        saveDir = opendir(savePath);
        assert(saveDir != NULL);
        closedir(saveDir);
    }
}

static size_t file_size(FILE *file)
{
    fseek(file, 0, SEEK_END);
    return ftell(file);
}

//Generate serialize and deserialize functions for integer types
#define X(typename)                                                             \
static void serialize_##typename(byte **dst, typename x)                        \
{                                                                               \
    for (int i = 0; i < sizeof(x); i++)                                         \
        (*dst)[i] = (x >> (CHAR_BIT * (sizeof(x) - i - 1))) & UCHAR_MAX;        \
    (*dst) += sizeof(x);                                                        \
}                                                                               \
                                                                                \
static typename deserialize_##typename(byte **src)                              \
{                                                                               \
    typename x = 0;                                                             \
                                                                                \
    for (int i = 0; i < sizeof(x); i++)                                         \
        x |= (*src)[i] << (CHAR_BIT * (sizeof(x) - i - 1));                     \
    (*src) += sizeof(x);                                                        \
    return x;                                                                   \
}

X(int32_t)
X(uint32_t)
X(uint8_t)

#undef X

static void serialize_string(byte **dst, const char *string, int length)
{
    for (int i = 0; i < length; i++)
        *((*dst)++) = string[i];
}

static void deserialize_string(byte **src, char *dst, int length)
{
    for (int i = 0; i < length; i++)
        dst[i] = *((*src)++);
}

static size_t calc_save_size(struct SaveFile *save)
{
    size_t size = 0;
    
    size += sizeof(fileMagic) - 1;  //file signature
    size += SAVENAME_MAX;           //name
    size += SEED_MAX;               //seed
    size += 3 * sizeof(int32_t);    //spawn location
    size += sizeof(uint32_t);       //number of modified chunks
    
    //chunk data
    for (int i = 0; i < save->modifiedChunksCount; i++)
    {
        size += sizeof(int32_t);   //x
        size += sizeof(int32_t);   //z
        size += sizeof(uint32_t);  //number of modified blocks
        size += 4;                 //TODO: Find out why I need 4 extra bytes
        
        //block data
        size += save->modifiedChunks[i].modifiedBlocksCount * 4 * sizeof(uint8_t);
    }
    
    return size;
}

static void read_save(struct SaveFile *save, byte *buffer, size_t bufSize)
{
    byte *ptr = buffer;
    byte *blockData;
    const int magicSize = sizeof(fileMagic);
    char magic[magicSize];
    
    //Verify magic value
    deserialize_string(&ptr, magic, magicSize - 1);
    magic[magicSize - 1] = '\0';
    if (strcmp(magic, fileMagic))
    {
        file_log("file_load_world(): magic value does not match (expected '%s', got '%s')", fileMagic, magic);
        return;
    }
    
    //Read name and seed
    deserialize_string(&ptr, save->name, SAVENAME_MAX);
    deserialize_string(&ptr, save->seed, SEED_MAX);
    
    //Read spawn location
    save->spawnX = deserialize_int32_t(&ptr);
    save->spawnY = deserialize_int32_t(&ptr);
    save->spawnZ = deserialize_int32_t(&ptr);
    
    //Read modified chunk data
    save->modifiedChunksCount = deserialize_uint32_t(&ptr);
    save->modifiedChunks = malloc(save->modifiedChunksCount * sizeof(struct ChunkModification));
    blockData = ptr + save->modifiedChunksCount * CHUNK_MOD_SIZE;
    for (int i = 0; i < save->modifiedChunksCount; i++)
    {
        struct ChunkModification *chunkMod = &save->modifiedChunks[i];
        
        chunkMod->x = deserialize_int32_t(&ptr);
        chunkMod->z = deserialize_int32_t(&ptr);
        chunkMod->modifiedBlocksCount = deserialize_uint32_t(&ptr);
        chunkMod->modifiedBlocks = malloc(chunkMod->modifiedBlocksCount * sizeof(struct BlockModification));
        
        //Read modified block data
        for (int j = 0; j < chunkMod->modifiedBlocksCount; j++)
        {
            struct BlockModification *blockMod = &chunkMod->modifiedBlocks[j];
            
            blockMod->x = deserialize_uint8_t(&blockData);
            blockMod->y = deserialize_uint8_t(&blockData);
            blockMod->z = deserialize_uint8_t(&blockData);
            blockMod->type = deserialize_uint8_t(&blockData);
        }
    }
    
    assert(bufSize >= blockData - buffer);
}

static void write_save(struct SaveFile *save, byte *buffer, size_t bufSize)
{
    byte *ptr = buffer;
    byte *blockData;
    char nameBuffer[SAVENAME_MAX] = {'\0'};
    char seedBuffer[SEED_MAX] = {'\0'};
    
    //Write magic value
    serialize_string(&ptr, fileMagic, strlen(fileMagic));
    
    //Write name
    strcpy(nameBuffer, save->name);
    serialize_string(&ptr, nameBuffer, SAVENAME_MAX);
    
    //Write seed
    strcpy(seedBuffer, save->seed);
    serialize_string(&ptr, seedBuffer, SEED_MAX);
    
    //Write spawn location
    serialize_int32_t(&ptr, save->spawnX);
    serialize_int32_t(&ptr, save->spawnY);
    serialize_int32_t(&ptr, save->spawnZ);
    
    //Write modified chunk data
    serialize_uint32_t(&ptr, save->modifiedChunksCount);
     
    blockData = ptr + save->modifiedChunksCount * CHUNK_MOD_SIZE;
    for (int i = 0; i < save->modifiedChunksCount; i++)
    {
        struct ChunkModification *chunkMod = &save->modifiedChunks[i];
        
        serialize_int32_t(&ptr, chunkMod->x);
        serialize_int32_t(&ptr, chunkMod->z);
        serialize_uint32_t(&ptr, chunkMod->modifiedBlocksCount);
        
        //Write modified block data
        for (int j = 0; j < chunkMod->modifiedBlocksCount; j++)
        {
            struct BlockModification *blockMod = &chunkMod->modifiedBlocks[j];
            
            serialize_uint8_t(&blockData, blockMod->x);
            serialize_uint8_t(&blockData, blockMod->y);
            serialize_uint8_t(&blockData, blockMod->z);
            serialize_uint8_t(&blockData, blockMod->type);
        }
    }
    
    assert(bufSize >= blockData - buffer);  //Make darn sure our buffer was the correct size
}

//We're testing the GameCube memory card saving on Wii for now
#undef PLATFORM_WII
#define PLATFORM_GAMECUBE

#if defined(PLATFORM_WII)

void file_init(void)
{
    assert(fatInitDefault());
    assert(can_open_root_dir());
    ensure_worlds_dir();
    remove(logFilePath);
}

void file_log(const char *fmt, ...)
{
    FILE *logFile = fopen(logFilePath, "a");
    va_list args;
    
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    va_end(args);
    fputs("\r\n", logFile);
    fclose(logFile);
}

void file_enumerate(bool (*callback)(const char *filename))
{
    DIR *saveDir = opendir(savePath);
    struct dirent *d;
    
    assert(saveDir != NULL);
    while ((d = readdir(saveDir)) != NULL)
    {
        if (d->d_name[0] != '.')
        {
            if (!callback(d->d_name))
                break;
        }
    }
    closedir(saveDir);
}

void file_load_world(struct SaveFile *save, const char *name)
{
    char *path = malloc(strlen(savePath) + 1 + strlen(name) + 1);
    FILE *file;
    size_t size;
    byte *buffer;
    
    strcpy(path, savePath);
    strcat(path, "/");
    strcat(path, name);
    
    file_log("file_load_world(): loading world '%s' from file '%s'", name, path);
    
    file = fopen(path, "r");
    assert(file != NULL);
    size = file_size(file);
    fseek(file, 0, SEEK_SET);
    buffer = calloc(size, 1);
    fread(buffer, 1, size, file);
    read_save(save, buffer, size);
    free(buffer);
    fclose(file);
}

void file_save_world(struct SaveFile *save)
{
    char *path = malloc(strlen(savePath) + 1 + strlen(save->name) + 1);
    FILE *file;
    size_t size;
    byte *buffer;
    
    assert(strlen(save->name) > 0);
    assert(strlen(save->seed) > 0);
    strcpy(path, savePath);
    strcat(path, "/");
    strcat(path, save->name);
    
    file_log("file_save_world(): saving world '%s' to file '%s'", save->name, path);
    
    size = calc_save_size(save);
    buffer = malloc(size);
    file = fopen(path, "w");
    assert(file != NULL);
    write_save(save, buffer, size);
    fwrite(buffer, size, 1, file);
    free(buffer);
    fclose(file);
}

void file_delete(const char *name)
{
    char *path = malloc(strlen(savePath) + 1 + strlen(name) + 1);
    
    strcpy(path, savePath);
    strcat(path, "/");
    strcat(path, name);
    
    file_log("file_delete(): deleting file '%s'", path);
    
    remove(path);
}

#elif defined(PLATFORM_GAMECUBE)

static const char gameCode[] = "CCRA";
static const char makerCode[] = "00";
static u8 sysWorkArea[CARD_WORKAREA] ATTRIBUTE_ALIGN(32);

static void card_remove_callback(s32 channel, s32 result)
{
    char chanLetter = '?';
    
    if (channel == CARD_SLOTA)
        chanLetter = 'A';
    else if (channel == CARD_SLOTB)
        chanLetter = 'B';
    file_log("memory card was removed from slot %c", chanLetter);
    CARD_Unmount(channel);
}

void file_init(void)
{
    s32 status;
    
    //Temporary: remove logging capabilities when we actually run on the GameCube
    assert(fatInitDefault());
    assert(can_open_root_dir());
    remove(logFilePath);
    
    status = CARD_Init(gameCode, makerCode);
    file_log("CARD_Init returned %i", status);
    /*
    status = CARD_Mount(CARD_SLOTA, sysWorkArea, card_remove_callback);
    file_log("CARD_Mount returned %i", status);
    */
}

void file_log(const char *fmt, ...)
{
    FILE *logFile = fopen(logFilePath, "a");
    va_list args;
    
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    va_end(args);
    fputs("\r\n", logFile);
    fclose(logFile);
}

void file_enumerate(bool (*callback)(const char *filename))
{
    /*
    card_dir cardDir;
    s32 status;
    
    status = CARD_FindFirst(CARD_SLOTA, &cardDir, false);
    while (status != CARD_ERROR_NOFILE)
    {
        file_log("file_enumerate(): found file '%s'", cardDir.filename);
        callback((const char *)cardDir.filename);
        status = CARD_FindNext(&cardDir);
    }
    */
}

void file_load_world(struct SaveFile *save, const char *name)
{
    /*
    card_file file;
    s32 status;
    byte *buffer;
    char temp[16];
    
    assert(false);
    file_log("file_load_world()");
    
    status = CARD_Open(CARD_SLOTA, name, &file);
    file_log("CARD_Open returned %i", status);
    if (status == CARD_ERROR_READY)
    {
        file_log("file size = %i", file.len);
        buffer = malloc(file.len);
        
        CARD_Read(&file, buffer, file.len, 0);
        
        //Check what was written
        strncpy(temp, (char *)buffer, 16);
        temp[15] = '\0';
        file_log("temp is '%s'", temp);
        
        read_save(save, buffer, file.len);
        
        free(buffer);
        CARD_Close(&file);
    }
    */
}

static inline int round_up(int number, int multiple)
{
    return ((number + multiple - 1) / multiple) * multiple;
}

void file_save_world(struct SaveFile *save)
{
    /*
    card_file file;
    s32 status;
    u32 sectorSize;
    size_t size;
    size_t fileSize;
    byte *buffer;
    char temp[16];
    
    file_log("file_save_world()");
    
    status = CARD_GetSectorSize(CARD_SLOTA, &sectorSize);
    file_log("CARD_SectorSize returned %i", status);
    size = calc_save_size(save);
    fileSize = round_up(size, sectorSize);
    buffer = malloc(fileSize);
    status = CARD_Open(CARD_SLOTA, save->name, &file);
    file_log("CARD_Open returned %i", status);
    if (status == CARD_ERROR_NOFILE)
    {
        //File doesn't exist. Create it.
        file_log("file '%s' does not exist. creating it... size = %i", save->name, size);
        status = CARD_Create(CARD_SLOTA, save->name, fileSize, &file);
        file_log("CARD_Create returned %i", status);
    }
    
    write_save(save, buffer, fileSize);
    
    strncpy(temp, (char *)buffer, 16);
    temp[15] = '\0';
    file_log("temp is %s", temp);
    
    status = CARD_Write(&file, buffer, fileSize, 0);
    file_log("CARD_Write returned %i", status);
    
    //Verify data written
    status = CARD_Read(&file, buffer, fileSize, 0);
    file_log("CARD_Read returned %i", status);
    
    memset(temp, '\0', 16);
    strncpy(temp, (char *)buffer, 32);
    temp[15] = '\0';
    file_log("temp is now %s", temp);
    
    free(buffer);
    status = CARD_Close(&file);
    file_log("CARD_Close returned %i", status);
    */
}

void file_delete(const char *name)
{
    /*
    file_log("deleting file '%s'", name);
    CARD_Delete(CARD_SLOTA, name);
    */
}

#endif
