typedef int Error; // any non-zero value is an error
typedef uint32_t R1Char; // 32bit unicode (or 20 bits, whatever)

#ifdef _WIN32
Error
FileSize(HANDLE fd, int64_t* size);
#else
Error
FileSize(int fd, int64_t* size);
#endif

struct Str
{
    const char* chars;
    size_t length;
};
// TODO constant string, growable etc.

// Compile library type, not Rust type (see RType)
struct Type
{
    char* name;
    BOOLEAN fixed_size;
    BOOLEAN pad[7];
    size_t size;
    void (*init)(void*);
    void (*cleanup)(void*, size_t n); // fclose, close, munmap, etc.
    void (*copy)(void* dest, const void* src, size_t n);
    void (*move)(void* dest, void* src, size_t n);
    BOOLEAN (*eq)(const void*, const void*);
    int (*cmp)(const void*, const void*);
    uint64_t (*hash)(const void*);
};

struct Allocator;
typedef struct Allocator Allocator;

struct AllocatorFn;
typedef struct AllocatorFn AllocatorFn;

struct AllocatorFn
{
    void* (*alloc)(Allocator*, size_t);
    void (*free)(Allocator*, void*);
};

struct Allocator
{
    AllocatorFn* fn;
};

struct Buf
typedef struct Buf Buf;

struct BufFn
{
    int (*push_back)(Buf*, const void*);
    size_t (*size)(Buf*);
    void (*pop_back)(Buf*);
    void (*clear)(Buf*);
    void (*reserve)(Buf*, size_t);
    void (*capacity)(Buf*, size_t);
    void* (*at)(Buf*, size_t);
};

struct Buf
{
    BufFn* fn;
    void* data;
    size_t capacity;    // elements
    size_t size;        // elements
    Type* element_type; // char, int, etc.
    Type* buf_type;     // e.g. mmap, heap, etc.
    Allocator* allocator;
//  BOOLEAN growable;
//  BOOLEAN statik;
//  BOOLEAN stack;
//  BOOLEAN heap;
//  BOOLEAN mmap;
//  BOOLEAN constant;
};

struct Mmap
{
    void* p;
    int64_t size;
};

struct File
{
    Str path;
#ifdef _WIN32
    HANDLE fd;
#else
    intptr_t fd;
#endif
};

void
FileCleanup(File* file)
{
#ifdef _WIN32
    if (file->fd)
    {
        CloseHandle(file->fd);
        file->fd = 0;
    }
#else
    if (file->fd >= 0)
    {
        close(file->fd);
        file->fd = -1;
    }
#endif
}

#ifdef _WIN32
Error
FileSize(HANDLE fd, int64_t* size)
{
    DWORD hi = 0;
    DWORD lo = GetFileSize(fd, &hi);
    if (lo == INVALID_FILE_SIZE)
    {
        DWORD err = GetLastError();
        if (err != NO_ERROR)
            return err;
    }
    *size = (((int64_t)hi) << 32) | lo;
    return 0;
}
#else
Error
FileSize(int fd, int64_t* size)
{
#if __CYGWIN__ || __linux__
    struct stat st = { 0 }; // TODO test more systems
    if (fstat (fd, &st))
#else
    struct stat64 st = { 0 }; // TODO test more systems
    if (fstat64 (fd, &st))
#endif
        return errno;
    *size = st.st_size;
    return 0;
}
#endif

Error
FileOpenR(const char* path, File* file)
{
#ifdef _WIN32
    if ((file->fd = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0)) == INVALID_HANDLE_VALUE)
        return GetLastError();
#else
    if ((file->fd = open(path, O_RDONLY)) == -1)
        return errno;
#endif
    return 0;
}

void
MmapCleanup(Mmap* map)
{
    if (map->p)
    {
#ifdef _WIN32
        UnmapViewOfFile(map->p);
#else
        munmap(map->p, map->size);
#endif
        map->p = 0;
    }
}

Error
MmapR(File* file, Mmap* map)
{
    Error err = {0};
#ifdef _WIN32
    void* p = {0};
    HANDLE section = {0};
#endif
    if ((err = FileSize(file->fd, &map->size)))
        return err;
    if (map->size > ~(size_t)0)
#ifdef _WIN32
        return ERROR_FILE_TOO_LARGE; // TODO
#else
        return E2BIG;
#endif

    if (map->size == 0)
        return 0; // sort of

#ifdef _WIN32
    if (!(section = CreateFileMapping(file->fd, 0, PAGE_WRITECOPY, 0, 0, 0)))
        return GetLastError();
    p = MapViewOfFile(section, FILE_MAP_COPY, 0, 0, 0);
    CloseHandle(section);
    if (!(map->p = p))
        return GetLastError();
#else
    if ((map->p = mmap(0, (size_t)map->size, PROT_READ, MAP_PRIVATE, file->fd, 0)) == MAP_FAILED)
        return errno;
#endif
    return 0;
}

