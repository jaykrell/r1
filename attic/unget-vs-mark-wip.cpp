// My first Rust compiler, r1.
// To start working from
// https://doc.rust-lang.org/stable/reference

#define _CRT_SECURE_NO_WARNINGS 1
#if _MSC_VER
#pragma warning (disable:4100) // unused parameter
#pragma warning (disable:4102) // unused label
#pragma warning (disable:4127) // conditional expression is constant
#pragma warning (disable:4201) // nameless struct/union
#pragma warning (disable:4355) // this used in base member initializer list
#pragma warning (disable:4365) // integer type mixups
#pragma warning (disable:4371) // layout change from previous compiler version
#pragma warning (disable:4480) // non-standard extension
#pragma warning (disable:4505) // unused static function
#pragma warning (disable:4514) // unused function
#pragma warning (disable:4571) // catch(...)
#pragma warning (disable:4616) // disable unknown warning (for older compiler)
#pragma warning (disable:4619) // disable unknown warning (for older compiler)
#pragma warning (disable:4625) // copy constructor implicitly deleted
#pragma warning (disable:4626) // assignment implicitly deleted
#pragma warning (disable:4668) // #if not_defined as #if 0
#pragma warning (disable:4668) // #if not_defined is #if 0
#pragma warning (disable:4706) // assignment within condition
#pragma warning (disable:4710) // function not inlined
#pragma warning (disable:4774) // printf used without constant format
#pragma warning (disable:4820) // padding
#pragma warning (disable:4820) // padding added
#pragma warning (disable:4820) // ucrt\malloc.h(45): warning C4820: '_heapinfo': '4' bytes padding added after data member '_heapinfo::_useflag'
#pragma warning (disable:5039) // exception handling and function pointers
#pragma warning (disable:5045) // compiler will/did insert Spectre mitigation
#endif
#if __GNUC__ || __clang__
#pragma GCC diagnostic ignored "-Wunused-label"
#endif

#ifdef _WIN32
typedef unsigned __int64 uint64_t;
typedef __int64 int64_t;
#else
#include <stdint.h>
#endif
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
typedef unsigned char BOOLEAN;
#define TRUE (1)
#define FALSE (0)
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif
#include <errno.h>
#include <unordered_map>

typedef uint32_t R1Char; // 32bit unicode (or 20 bits, whatever)

#ifdef _WIN32
int
FileSize(HANDLE fd, int64_t* size);
#else
int
FileSize(int fd, int64_t* size);
#endif

struct Str
{
    char* chars;
    size_t length;
};
// TODO constant string, growable etc.

// Compile library type, not Rust type (see RType)
struct Type
{
    char* name;
    size_t size;
    void (*init)(void*);
    void (*cleanup)(void*); // fclose, close, munmap, etc.
    BOOLEAN (*eq)(void*, void*);
    int (*cmp)(void*, void*);
};

struct Buf
{
    void* data;
    size_t allocated;   // bytes
    size_t size;        // bytes
    Type* element_type; // char, int, etc.
    Type* buf_type;     // e.g. mmap, heap, etc.
    BOOLEAN growable;
    BOOLEAN statik;
    BOOLEAN stack;
    BOOLEAN heap;
    BOOLEAN map;
    BOOLEAN constant;
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

template <typename T>
struct Stream
{
    virtual int Get(T*);
};

typedef Stream<unsigned char> ByteStream;
typedef Stream<R1Char> UnicodeStream;

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
int
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
int
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

int
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

int
MmapR(File* file, Mmap* map)
{
    int err = {0};
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

enum R1TokenType
{
    R1TokenKeyword    = 1,
    R1TokenIdentifer   = 2,
    R1TokenLiteral     = 3,
    R1TokenLifetime    = 4,
    R1TokenPunctuation = 5,
    R1TokenDelimeter   = 6
};
typedef enum R1TokenType R1TokenType;

enum R1KeywordType
{
    R1KeywordStrict   = 1,
    R1KeywordReserved = 2,
    R1KeywordWeak     = 3
};
typedef enum R1KeywordType R1KeywordType;

enum R1LiteralType
{
    R1LiteralChar          = 1, // 'c'
    R1LiteralStr           = 2, // "c"
    R1LiteralRawStr        = 3, // r#"c"#
    R1LiteralByte          = 4, // b'c'
    R1LiteralByteString    = 5, // b"c"
    R1LiteralRawByteString = 6, // br#"c"#
};
typedef enum R1LiteralType R1LiteralType;

struct R1Token
{
    R1TokenType type;
    Str str;
};

struct R1Keyword
{
    R1Token token;
    R1KeywordType type;
};

#define PASTE(x, y) PASTE_(x, y)
#define PASTE_(x, y) x ## y

#define CONSTANT_STRING_AND_LENGTH(a) a, sizeof(a) - 1

#define R1_KEYWORD_CID(cid)                 PASTE(r1kw, cid)
#define R1_KEYWORD_INIT(a, type)            {{R1TokenKeyword, {CONSTANT_STRING_AND_LENGTH(a)}}, type}
#define R1_KEYWORD_CID_INIT(cid, a, type)   R1_KEYWORD_CID(cid) R1_KEYWORD_INIT(a, type)

#define R1_KEYWORD_STRICT(cid, a)           R1_KEYWORD(cid, a, R1KeywordStrict)
#define R1_KEYWORD_RESERVED(cid, a)         R1_KEYWORD(cid, a, R1KeywordReserved)
#define R1_KEYWORD_WEAK(cid, a)             R1_KEYWORD(cid, a, R1KeywordWeak)

#define R1_KEYWORDS                             \
    /* strict keywords */                       \
    R1_KEYWORD_STRICT(As, "as")                 \
    R1_KEYWORD_STRICT(Break, "break")           \
    R1_KEYWORD_STRICT(Const, "const")           \
    R1_KEYWORD_STRICT(Continue, "continue")     \
    R1_KEYWORD_STRICT(Crate, "crate")           \
    R1_KEYWORD_STRICT(Else, "else")             \
    R1_KEYWORD_STRICT(Enum, "enum")             \
    R1_KEYWORD_STRICT(Extern, "extern")         \
    R1_KEYWORD_STRICT(False, "false")           \
    R1_KEYWORD_STRICT(Fn, "fn")                 \
    R1_KEYWORD_STRICT(For, "for")               \
    R1_KEYWORD_STRICT(If, "if")                 \
    R1_KEYWORD_STRICT(Impl, "impl")             \
    R1_KEYWORD_STRICT(In, "in")                 \
    R1_KEYWORD_STRICT(Let, "let")               \
    R1_KEYWORD_STRICT(Loop, "loop")             \
    R1_KEYWORD_STRICT(Match, "match")           \
    R1_KEYWORD_STRICT(Mod, "mod")               \
    R1_KEYWORD_STRICT(Move, "move")             \
    R1_KEYWORD_STRICT(Mut, "mut")               \
    R1_KEYWORD_STRICT(Pub, "pub")               \
    R1_KEYWORD_STRICT(Ref, "ref")               \
    R1_KEYWORD_STRICT(Return, "return")         \
    R1_KEYWORD_STRICT(SelfValue, "self")        \
    R1_KEYWORD_STRICT(SelfType, "Self")         \
    R1_KEYWORD_STRICT(Static, "static")         \
    R1_KEYWORD_STRICT(Struct, "struct")         \
    R1_KEYWORD_STRICT(Super, "super")           \
    R1_KEYWORD_STRICT(Trait, "trait")           \
    R1_KEYWORD_STRICT(True, "true")             \
    R1_KEYWORD_STRICT(Type, "type")             \
    R1_KEYWORD_STRICT(Unsafe, "unsafe")         \
    R1_KEYWORD_STRICT(Use, "use")               \
    R1_KEYWORD_STRICT(Where, "where")           \
    R1_KEYWORD_STRICT(While, "while")           \
                                                \
/* Additional 2018 strict keywords */           \
    R1_KEYWORD_STRICT_2018(Async, "async")      \
    R1_KEYWORD_STRICT_2018(Await, "await")      \
                                                \
/* reserved keywords */                         \
    R1_KEYWORD_RESERVED(Abstract, "abstract")   \
    R1_KEYWORD_RESERVED(Become, "become")       \
    R1_KEYWORD_RESERVED(Box, "box")             \
    R1_KEYWORD_RESERVED(Do, "do")               \
    R1_KEYWORD_RESERVED(Final, "final")         \
    R1_KEYWORD_RESERVED(Macro, "macro")         \
    R1_KEYWORD_RESERVED(Override, "override")   \
    R1_KEYWORD_RESERVED(Priv, "priv")           \
    R1_KEYWORD_RESERVED(Typeof, "typeof")       \
    R1_KEYWORD_RESERVED(Unsized, "unsized")     \
    R1_KEYWORD_RESERVED(Virtual, "virtual")     \
    R1_KEYWORD_RESERVED(Yield, "yield")         \
                                                \
/* reserved 2018 keywords */                    \
    R1_KEYWORD_RESERVED_2018(Try, "try")        \
                                                \
/* weak keywords (context senstive) */          \
    R1_KEYWORD_WEAK(Union, "union")             \
    R1_KEYWORD_WEAK(StaticLifetime, "'static")  \
                                                \
    R1_KEYWORD_WEAK_2015(Dyn, "dyn")            \
    R1_KEYWORD_STRICT_2018(Dyn, "dyn")

#define R1_NOTHING /* nothing */

// 2015 keywords
union
{
#undef R1_KEYWORD_STRICT_2018
#undef R1_KEYWORD_RESERVED_2018
#undef R1_KEYWORD_RESERVED_2018
#undef R1_KEYWORD_WEAK_2015
#undef R1_KEYWORD

#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
#define R1_KEYWORD_STRICT_2018(cid, a)   /* nothing */
#define R1_KEYWORD_WEAK_2015(cid, a)     R1_KEYWORD(cid, R1_NOTHING, R1_NOTHING)
#define R1_KEYWORD_RESERVED_2018(cid, a) /* nothing */
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1AllKeywords2015;

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD

#define R1_KEYWORD_STRICT(cid, a)        R1_KEYWORD(cid, type, R1_NOTHING)
#define R1_KEYWORD_WEAK(cid, a)          /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)      /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1StrictKeywords2015;

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD

#define R1_KEYWORD_STRICT(cid, a)        /* nothing */
#define R1_KEYWORD_WEAK(cid, a)          R1_KEYWORD(cid, type, R1_NOTHING)
#define R1_KEYWORD_RESERVED(cid, a)      /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1WeakKeywords2015;

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD

#define R1_KEYWORD_STRICT(cid, a)        /* nothing */
#define R1_KEYWORD_WEAK(cid, a)          /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)      R1_KEYWORD(cid, type, R1_NOTHING)

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1ReservedKeywords2015;


// 2018 keywords

#undef R1_KEYWORD_STRICT_2018
#undef R1_KEYWORD_WEAK_2015
#undef R1_KEYWORD_RESERVED_2018

#define R1_KEYWORD_WEAK_2015(cid, a)     /* nothing */
#define R1_KEYWORD_STRICT_2018(cid, a)   R1_KEYWORD_STRICT(cid, a)
#define R1_KEYWORD_RESERVED_2018(cid, a) R1_KEYWORD_RESERVED(cid, a)

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#define R1_KEYWORD_WEAK(cid, a)     R1_KEYWORD(cid, R1_NOTHING, R1_NOTHING)
#define R1_KEYWORD_STRICT(cid, a)   R1_KEYWORD(cid, R1_NOTHING, R1_NOTHING)
#define R1_KEYWORD_RESERVED(cid, a) R1_KEYWORD(cid, R1_NOTHING, R1_NOTHING)

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)    R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1AllKeywords2018;

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD
#define R1_KEYWORD_STRICT(cid, a)        R1_KEYWORD(cid, R1_NOTHING, R1_NOTHING)
#define R1_KEYWORD_WEAK(cid, a)          /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)      /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1StrictKeywords2018;

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#define R1_KEYWORD_STRICT(cid, a)        /* nothing */
#define R1_KEYWORD_WEAK(cid, a)          R1_KEYWORD(cid, R1_NOTHING, R1_NOTHING)
#define R1_KEYWORD_RESERVED(cid, a)      /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1WeakKeywords2018;

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#define R1_KEYWORD_STRICT(cid, a)        /* nothing */
#define R1_KEYWORD_WEAK(cid, a)          /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)      R1_KEYWORD(cid, type, R1_NOTHING)

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1ReservedKeywords2018;

struct R1;

// TODO a vector but start at the middle
template <typename T>
struct OffsetVector : std::deque<T>
{
};

template <typename T>
struct UngetStream : Stream<T>
{
    OffsetVector<T> unget;
    Stream<T> get;

    virtual int Get(T* p)
    {
        if (unget.size())
        {
            *p = unget.front();
            unget.pop_front();
            return;
        }
        return get.Get(p);
    }

    void Unget(const T& t)
    {
        unget.push_back(t);
    }
};

struct R1
{
    int version; // 2015 or 2018
    R1Char currentChar;
    Stream<R1Char> stream;
    File   file;
};

int
R1_IsRawStringLiteral(R1* r1, bool* result)
{
    LexMark mark = R1_LexMark(r1);

    *result = false;
    err = r1.CurrentChar(&ch);
    if (err)
        return err;
    if (ch != 'r')
        return 0;
    err = r1.NextChar(&ch);
    if (err)
        return err;
    if (ch != '#')
        return 0;
}

int
R1_NextChar(R1* r1, R1Char* ch)
{
    return -1;
}

int
R1_CurrentChar(R1* r1, R1Char* ch)
{
    return -1;
}

int
R1_CurrentToken(R1* r1, R1Token* token)
{
    return -1;
}

int
R1_NextToken(R1* r1, R1Token* token)
{
    return -1;
}

int
f1(const char* path)
{
    int err = {0};
    File file = {0};
    Mmap map = {0};

    printf("Rust2015 has strict/weak/reserved/all %d/%d/%d/%d keywords\n"
           "Rust2018 has strict/weak/reserved/all %d/%d/%d/%d keywords\n",
           (int)(sizeof(r1StrictKeywords2015) / sizeof(R1Keyword)),
           (int)(sizeof(r1WeakKeywords2015) / sizeof(R1Keyword)),
           (int)(sizeof(r1ReservedKeywords2015) / sizeof(R1Keyword)),
           (int)(sizeof(r1AllKeywords2015) / sizeof(R1Keyword)),
           (int)(sizeof(r1StrictKeywords2018) / sizeof(R1Keyword)),
           (int)(sizeof(r1WeakKeywords2018) / sizeof(R1Keyword)),
           (int)(sizeof(r1ReservedKeywords2018) / sizeof(R1Keyword)),
           (int)(sizeof(r1AllKeywords2018) / sizeof(R1Keyword)));

    if (path)
    {
        if ((err = FileOpenR(path, &file)))
            goto exit;

        if ((err = MmapR(&file, &map)))
            goto exit;

        if (map.p)
            printf("%c\n", ((char*)map.p)[0]);
    }

exit:
    FileCleanup(&file);
    MmapCleanup(&map);
    return err;
}

int main(int arc, char** argv)
{
    R1 r1 = { 2018 };

    for (int i = 1; i < argc; ++argv)
    {
        if (strcmp(argv[i], "--") == 0)
            break;
        if (strcmp(argv[i], "-2015") == 0)
            r1.
    }

//    if (argv[1])
    {
        f1(argv[1]);
    }
}

#endif
