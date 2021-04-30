// My first Rust compiler, r1.
// To start working from
// https://doc.rust-lang.org/stable/reference

#define _CRT_SECURE_NO_WARNINGS 1
#define NOMINMAX
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

#if _MSC_VER
typedef unsigned __int64 uint64_t;
typedef __int64 int64_t;
#else
#include <stdint.h>
#endif
#include <assert.h>
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
#include <vector>
#include <deque>
#include <string>
#include <string.h>
#include <set>
#include <limits>
#include <memory>
using namespace std;

typedef int Error; // any non-zero value is an error

typedef char32_t R1Char;

#ifdef _WIN32
Error
FileSize(HANDLE fd, uint64_t* size);
#else
Error
FileSize(int fd, uint64_t* size);
#endif

struct Str
{
    const char* chars;
    const char32_t* chars32;
    size_t length;
};
// TODO constant string, growable etc.

struct Mmap
{
    void* p;
    uint64_t size;

    Mmap();
    ~Mmap();
    void Cleanup();
};

struct File
{
    Str path;
#ifdef _WIN32
    HANDLE fd;
#else
    intptr_t fd;
#endif
    File();
    ~File();
    void Cleanup();
};

template <typename T>
struct Stream
{
    // TODO Streams should get to change the type/size of Position?
    virtual uint64_t Position() = 0;
    virtual Error SetPosition(uint64_t) = 0;
    virtual Error Get(T*) = 0;
};

typedef Stream<unsigned char> ByteStream;
typedef Stream<R1Char> UnicodeStream;

File::File()
#ifdef _WIN32
: fd(INVALID_HANDLE_VALUE)
#else
: fd(-1)
#endif
{
}

File::~File()
{
    Cleanup();
}

void
File::Cleanup()
{
#ifdef _WIN32
    if (fd)
    {
        CloseHandle(fd);
        fd = { };
    }
#else
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
#endif
}

#ifdef _WIN32
Error
FileSize(HANDLE fd, uint64_t* size)
{
    DWORD hi { };
    DWORD lo = GetFileSize(fd, &hi);
    if (lo == INVALID_FILE_SIZE)
    {
        DWORD err = GetLastError();
        if (err != NO_ERROR)
            return err;
    }
    *size = (((uint64_t)hi) << 32) | lo;
    return 0;
}
#else
Error
FileSize(int fd, uint64_t* size)
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

Mmap::Mmap() : p(0), size(0)
{
}

Mmap::~Mmap()
{
    Cleanup();
}

void
Mmap::Cleanup()
{
    if (p)
    {
#ifdef _WIN32
        UnmapViewOfFile(p);
#else
        munmap(p, size);
#endif
        p = { };
    }
}

Error
MmapR(File* file, Mmap* mmap)
{
    Error err = {0};
#ifdef _WIN32
    void* p = {0};
    HANDLE section = {0};
#endif
    if ((err = FileSize(file->fd, &mmap->size)))
        return err;
    if (mmap->size > std::numeric_limits<size_t>::max())
#ifdef _WIN32
        return ERROR_FILE_TOO_LARGE; // TODO
#else
        return E2BIG;
#endif

    if (mmap->size == 0)
        return 0; // success, sort of; no buffer, caller see size

#ifdef _WIN32
    if (!(section = CreateFileMapping(file->fd, 0, PAGE_WRITECOPY, 0, 0, 0)))
        return GetLastError();
    p = MapViewOfFile(section, FILE_MAP_COPY, 0, 0, 0);
    CloseHandle(section);
    if (!(mmap->p = p))
        return GetLastError();
#else
    if ((mmap->p = ::mmap(0, (size_t)mmap->size, PROT_READ, MAP_PRIVATE, file->fd, 0)) == MAP_FAILED)
        return errno;
#endif
    return 0;
}

template <typename T>
struct StreamOverMmap : Stream<T>
{
    T* position;
    T* end;
    Mmap mmap;
    bool first;

    StreamOverMmap();

    Error OpenR(const char* path);

    virtual Error Get(T*);
    virtual uint64_t Position();
    virtual Error SetPosition(uint64_t);
};

template <typename T>
StreamOverMmap<T>::StreamOverMmap() : position(0), end(0), first(true)
{
}

template <typename T>
Error
StreamOverMmap<T>::OpenR(const char* path)
{
    File file;
    Error err = FileOpenR(path, &file);
    if (err)
        return err;
    err = MmapR(&file, &mmap);
    if (err)
        return err;
    position = (T*)mmap.p;
    if (mmap.size % sizeof(T))
        return -1;
    end = position + mmap.size / sizeof(T);
    return 0;
}

template <typename T>
Error
StreamOverMmap<T>::Get(T* ch)
{
    assert(position <= end);
    if (!first)
        ++position;
    else
        first = false;
    assert(position <= end);
    if (position >= end)
        return -1;
    *ch = *position;
    return 0;
}

template <typename T>
Error
StreamOverMmap<T>::SetPosition(uint64_t pos)
{
    position = (T*)(intptr_t)pos; // safety?
    return 0;
}

template <typename T>
uint64_t
StreamOverMmap<T>::Position()
{
    return (uint64_t)(intptr_t)position;
}

struct UnicodeStreamOverByteStream : UnicodeStream
{
    ByteStream* byteStream;
    virtual Error Get(R1Char*);
    virtual uint64_t Position();
    virtual Error SetPosition(uint64_t);
};

Error
UnicodeStreamOverByteStream::Get(R1Char* ch)
{
    unsigned char ch0;
    Error err = byteStream->Get(&ch0);
    //if (ch < 0x80) // TODO decode UTF8
    {
        *ch = ch0;
    }
    return err;

}

uint64_t
UnicodeStreamOverByteStream::Position()
{
    return byteStream->Position();
}

Error
UnicodeStreamOverByteStream::SetPosition(uint64_t pos)
{
    return byteStream->SetPosition(pos);
}

enum R1TokenType
{
    R1TokenKeyword     = 1,
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
    R1LiteralString        = 2, // "c"
    R1LiteralRawString     = 3, // r#"c"#  any number of #, even zero, the same on each side
    R1LiteralByte          = 4, // b'c'
    R1LiteralByteString    = 5, // b"c"
    R1LiteralRawByteString = 6, // br#"c"# any number of #, even zero, the same on each side
};
typedef enum R1LiteralType R1LiteralType;

struct R1Token
{
    R1TokenType type { };
    Str str { };
};

struct R1Keyword
{
    R1Token token { };
    R1KeywordType type { };
};

struct R1Literal
{
    R1Token token { };
    R1LiteralType type { };
};

#define PASTE(x, y) PASTE_(x, y)
#define PASTE_(x, y) x ## y

#define CONSTANT_STRING_AND_LENGTH(a) a, sizeof(a) - 1
#define CONSTANT_STR(a)               a, PASTE(U, a), sizeof(a) - 1

// cid = C language identifier, or the end of one
#define R1_KEYWORD_CID(cid)             PASTE(r1kw, cid)
#define R1_KEYWORD_INIT(a, type)        {{R1TokenKeyword, {CONSTANT_STR(a)}}, type}

#define R1_KEYWORD_STRICT(cid, a)       R1_KEYWORD(cid, a, R1KeywordStrict)
#define R1_KEYWORD_RESERVED(cid, a)     R1_KEYWORD(cid, a, R1KeywordReserved)
#define R1_KEYWORD_WEAK(cid, a)         R1_KEYWORD(cid, a, R1KeywordWeak)

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
    R1_KEYWORD_STRICT_2018(Dyn, "dyn")          \

//#define R1_NOTHING /* nothing */

// 2015 keywords
union
{
#undef R1_KEYWORD_STRICT_2018
#undef R1_KEYWORD_RESERVED_2018
#undef R1_KEYWORD_RESERVED_2018
#undef R1_KEYWORD_WEAK_2015

#define R1_KEYWORD_STRICT_2018(cid, a)   /* nothing */
#define R1_KEYWORD_WEAK_2015(cid, a)     R1_KEYWORD_WEAK(cid, a)
#define R1_KEYWORD_RESERVED_2018(cid, a) /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1AllKeywords2015
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD

#define R1_KEYWORD_STRICT(cid, a)        R1_KEYWORD(cid, a, R1KeywordStrict)
#define R1_KEYWORD_WEAK(cid, a)          /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)      /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1StrictKeywords2015
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD

#define R1_KEYWORD_STRICT(cid, a)        /* nothing */
#define R1_KEYWORD_WEAK(cid, a)          R1_KEYWORD(cid, a, R1KeywordWeak)
#define R1_KEYWORD_RESERVED(cid, a)      /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1WeakKeywords2015

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD

#define R1_KEYWORD_STRICT(cid, a)       /* nothing */
#define R1_KEYWORD_WEAK(cid, a)         /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)     R1_KEYWORD(cid, a, R1KeywordReserved)

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1ReservedKeywords2015
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

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
#define R1_KEYWORD_STRICT(cid, a)       R1_KEYWORD(cid, a, R1KeywordStrict)
#define R1_KEYWORD_RESERVED(cid, a)     R1_KEYWORD(cid, a, R1KeywordReserved)
#define R1_KEYWORD_WEAK(cid, a)         R1_KEYWORD(cid, a, R1KeywordWeak)

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)    R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1AllKeywords2018
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#undef R1_KEYWORD
#define R1_KEYWORD_STRICT(cid, a)       R1_KEYWORD(cid, a, R1KeywordStrict)
#define R1_KEYWORD_WEAK(cid, a)         /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)     /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1StrictKeywords2018
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#define R1_KEYWORD_STRICT(cid, a)       /* nothing */
#define R1_KEYWORD_WEAK(cid, a)         R1_KEYWORD(cid, a, R1KeywordWeak)
#define R1_KEYWORD_RESERVED(cid, a)     /* nothing */

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1WeakKeywords2018
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

union
{
#undef R1_KEYWORD_STRICT
#undef R1_KEYWORD_WEAK
#undef R1_KEYWORD_RESERVED
#define R1_KEYWORD_STRICT(cid, a)       /* nothing */
#define R1_KEYWORD_WEAK(cid, a)         /* nothing */
#define R1_KEYWORD_RESERVED(cid, a)     R1_KEYWORD(cid, a, R1KeywordReserved)

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type)         R1Keyword R1_KEYWORD_CID(cid);
    struct { R1_KEYWORDS } named;

#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) + 1
    R1Keyword array[0 R1_KEYWORDS];
} r1ReservedKeywords2018
#undef R1_KEYWORD
#define R1_KEYWORD(cid, a, type) R1_KEYWORD_INIT(a, type),
    = {{ R1_KEYWORDS }};

#if 0
// TODO a vector but start at the middle
template <typename T>
struct OffsetVector : std::deque<T>
{
};
#endif

#if 0
template <typename T>
struct UngetStream : Stream<T>
{
    OffsetVector<T> unget;
    Stream<T> get;

    virtual Error Get(T* p)
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
#endif

#if 0
struct Hasher // TODO
{
    uint64_t value { };

    void Reset()
    {
        value = { };
    }

    void operator()(R1Char ch)
    {
        value *= 131;
        value += ch;
    }
};
#endif

// Keyword detector while scanning identifier.
template <typename Element, Element Min, Element Max, typename Leaf>
struct Tri
{
    static size_t Index(Element e)
    {
        // TODO return map[e - Min] to make smaller
        return e - Min;
    }

    void Add(const Element* str, size_t length, Leaf* leaf)
    {
        Tri* t = this;
        for (size_t i = { }; i < length; ++i)
        {
            assert(str[i] >= Min);
            assert(str[i] <= Max);
            size_t index = Index(str[i]);

            if (i == length - 1)
            {
                t->leaves[index] = leaf;
            }
            else
            {
                if (!t->next[index])
                {
                    t->next[index].reset(new Tri());
                }
                t = t->next[index].get();
            }
        }
    }

    static void Next(Element e, Tri** t, Leaf** leaf)
    {
        if (!*t)
        {
            return;
        }
        else if (e < Min || e > Max)
        {
            *t = { };
            *leaf = { };
            return;
        }
        size_t index = Index(e);

        // To traverse from type to typeof, we must look at leaves and next.
        *leaf = (*t)->leaves[index];
        *t = (*t)->next[index].get();
    }

    // Because a string can be a substring of another ("type" vs. "typeof"), union does not work here.
    unique_ptr<Tri> next[Max - Min + 1];
    Leaf* leaves[Max - Min + 1] { };
};

struct R1
{
    R1();

    void ReportError(const char* err)
    {
        fprintf(stderr, "%s\n", err);
        exit(1); // TODO
    }

    int version = 2018; // 2015 or 2018
    // TODO Tri should support set of values, or multiple ranges, not just one range
    // That is, the values between 'Z' and 'a' should not use space.
    // This could be as simple as having the one range, and instead of
    // subtracing Min, map to sense.
    // If the set was for example 'Z' + a-z, Z would map to 0, a to 1, etc.
    typedef Tri<R1Char, 'S', 'z', R1Keyword> KeywordTri;
    KeywordTri keywordTri;
    R1Char currentChar;
    char currentByte;
    string path;
    StreamOverMmap<unsigned char> byteStream;
    UnicodeStreamOverByteStream unicodeStream;
    struct
    {
        // Instantiate each token type as a micro optimization
        // to avoid at least e.g. setting the type.
        R1Literal literalChar;
        R1Literal literalByte;
        R1Literal literalString
        R1Literal literalByteString
        R1Literal literalRawString
        R1Literal literalRawByteString
        R1Token   dynamic; // TODO
    } tokens;
    R1Token* currentToken {};
    vector<R1Char> dynamicToken; // i.e. identifier or literal, not reserved word
    const char* potentialError { };
    //set<vector<R1Char>> exactStrictAndReservedKeywords;
    //set<uint64_t> hashedStrictAndReservedKeywords;
    //Hasher hash;

    Error LexIdentifierOrKeyword(R1Keyword** keyword, bool* result);

    Error LexLiteral(bool* result); // char, string, byte, raw, etc.

    Error CurrentChar(R1Char*);
    Error NextChar(R1Char*);
    Error CurrentToken(R1Token**);
    Error NextToken(R1Token**);
    Error f1(const char* path);
    void main(int argc, char** argv);
};

template <typename I>
bool
AllEqual(I begin, I end, R1Char ch)
{
    while (begin != end)
    {
        if (*begin++ != ch)
            return false;
    }
    return true;
}

template <typename T, T In, T Align>
struct RoundUpConstantToConstant
{
    enum { Value = (In % Align) ? (In + Align - (In % Align)) : In };
};

template <typename T, T Align>
struct RoundUpToConstant
{
    static T Value(T In)
    {
        return (In % Align) ? (In + Align - (In % Align)) : In;
    }
};

#if 0

template <typename Element, Element Min, Element Max>
struct Bitmap
{
    enum { Count = Max - Min + 1 };
    uint64_t bits[(Count + 63) / 64] = {};

    bool InInSet(Element ch)
    {
        if (ch < Min || ch > Max)
            return false;
        size_t index = ch / 64;
        size_t mod = ch % 64;
        return (bits[index] & mod) != 0;
    }
};

#endif

struct AsciiSet
{
    uint64_t bits[2];

    bool InInSet(R1Char ch)
    {
        return 0;
    }
};

template <typename T, T Min, T Max>
bool
IsInConstantRangeInclusive(T ch)
{
    return ch >= Min && ch <= Max;
}

template <typename T>
bool
IsUpper(T ch)
{
    return IsInConstantRangeInclusive<T, 'A', 'Z'>(ch);
}

template <typename T>
bool
IsLower(T ch)
{
    return IsInConstantRangeInclusive<T, 'a', 'z'>(ch);
}

template <typename T>
bool
IsAlpha(T ch)
{
    return IsUpper(ch) || IsLower(ch);
}

template <typename T>
bool
IsNumber(T ch)
{
    return IsInConstantRangeInclusive<T, '0', '9'>(ch);
}

template <typename T>
bool
IsAlphaNumeric(T ch)
{
    return IsNumber(ch) || IsAlpha(ch);
}

bool
IsIdentifierChar(R1Char ch)
{
    return IsNumber(ch) || IsAlpha(ch) || ch == '_';
}

Error
R1::LexIdentifierOrKeyword(R1Keyword** keyword, bool* result)
{
    // TODO Likely we can reduce or avoid stream positioning.
    potentialError = { };
    *keyword = { };
    KeywordTri* tri = &keywordTri;
    dynamicToken.clear();
    uint64_t position = unicodeStream.Position();
    R1Char ch0 { };
    R1Char ch { };
    *result = false;
    Error err = CurrentChar(&ch0);
    if (err || (ch0 != '_' && !IsAlpha(ch0)))
        goto exit;

    tri->Next(ch0, &tri, keyword);

    //hash(ch0); // aid keyword detection
    dynamicToken.push_back(ch0);

    err = NextChar(&ch);
    if (err)
        goto exit;

    while (IsIdentifierChar(ch))
    {
        // characters seen beyond keyword
        // But keep in mind, type => typeo => typeof
        *keyword = { };

        if (tri)
            tri->Next(ch, &tri, keyword);

        //hash(ch);
        dynamicToken.push_back(ch);
        err = NextChar(&ch);
        if (err)
            break;
    }

    if (ch0 == '_' && dynamicToken.size() == 1)
    {
        ReportError("Identifer '_' is not allowed");
        goto exit;
    }

    *result = true;

exit:
    if (!*result)
        unicodeStream.SetPosition(position);
    return err;
}

Error
R1::LexLiteral(bool* result)
{
    // TODO Likely we can reduce or avoid stream positioning.
    // Likely multiple checks can be combined (work in progress).
    //
    // TODO Handle escapes.
    // TODO Limit byte values to < 256.
    //
    potentialError = { };
    dynamicToken.clear();
    uint64_t position;
    R1Char ch { };
    *result = { };
    size_t pounds;
    R1LiteralType type { };
    bool raw { };

    // All literals are at least 3 characters, and their type
    // can be determined from 1 or 2.
    //
    // Failure to read 2 characters here is not fatal.
    // It could be a different shorter token.

    Error err = CurrentChar(&ch);
    if (err)
        goto err;

    switch (ch)
    {
    case '\'':
    case '\"':
    case 'r':
    case 'b':
        break;
    default:
        return err;
    }
    position = unicodeStream.Position();
    pounds = { };

    if (ch == '\'')
    {
        err = NextChar(&ch);
        if (err)
        {
            err = 0;
            goto exit;
        }
        if (ch != '\'')
        {
            ReportError("invalid character literal");
            goto exit;
        }
        currentChar = ch;
        currentByte = (char)ch; // TODO lossy
        currentToken = &tokens.literalChar.token;
        *result = true;
        goto exit;
    }
    if (ch == '\"')
    {
        //type = R1LiteralString;
        currentToken = &tokens.literalString.token;
    }
    else if (ch == 'r')
    {
        //type = R1LiteralRawString;
        raw = true;
        currentToken = &tokens.literalRawString.token;
    }
    else if (ch == 'b')
    {
        err = NextChar(&ch);
        if (err)
        {
            err = 0;
            goto exit;
        }
        if (ch == 'r')
        {
            raw = true;
            currentToken = &tokens.literalRawByteString.token;
        }
        else if (ch == '\"')
        {
            currentToken = &tokens.literalByteString.token;
        }
        else if (ch == '\'')
        {
            err = NextChar(&ch);
            if (err)
            {
                err = 0;
                goto exit;
            }
            currentChar = ch;
            currentByte = (char)ch;

            err = NextChar(&ch);
            if (err)
            {
                err = 0;
                goto exit;
            }
            if (ch != '\'')
            {
                ReportError("invalid byte literal");
                goto exit;
            }

            currentToken = &tokens.literalByte.token;
            *result = true;
            goto exit;
        }
        // Token starts 'b' but not b['"r], could be almost anything but is not a literal.
        goto exit;
    }

    if (raw)
    {
        do
        {
            err = NextChar(&ch);
            if (err)
                goto exit;
            if (ch != '#')
                break;
            pounds += 1;
        }
    }
    if (ch != '"')
        goto exit;

    potentialError = "Unterminated raw string literal";

    // The token is terminated by " and the same number of #
    // If there are fewer #, then the " and # are part of the token.
    //
    // For example:
    //  r##"abc"##          => abc
    //  r###"abc"##def"###  => abc"##def
    //  r###"abc"##def"#### => abc"##def and then leftover # for additional scanning.
    //  r#""# => empty

    while (true)
    {
        err = NextChar(&ch);
        if (err)
            goto exit;
        dynamicToken.push_back(ch);
        if (dynamicToken.size() >= (1 + pounds) &&
            dynamicToken[dynamicToken.size() - pounds - 1] == '"' &&
            AllEqual(dynamicToken.end() - pounds, dynamicToken.end(), '#'))
        {
            dynamicToken.resize(dynamicToken.size() - 1 - pounds);
            *result = true;
            break;
        }
    }

exit:
    if (!*result)
        unicodeStream.SetPosition(position);
exit_fast:
    return err;
}

Error
R1::NextChar(R1Char* ch)
{
    Error err = unicodeStream.Get(ch);
    currentChar = *ch;
    return err;
}

Error
R1::CurrentChar(R1Char* ch)
{
    *ch = currentChar;
    return 0;
}

Error
R1::CurrentToken(R1Token** token)
{
    return -1;
}

Error
R1::NextToken(R1Token** token)
{
    return -1;
}

Error
R1::f1(const char* p)
{
    Error err = {0};

    if (version == 2015)
    {
        for (R1Keyword& keyword : r1StrictKeywords2015.array)
            keywordTri.Add(keyword.token.str.chars32, keyword.token.str.length, &keyword);
        for (R1Keyword& keyword : r1ReservedKeywords2015.array)
            keywordTri.Add(keyword.token.str.chars32, keyword.token.str.length, &keyword);
    }
    else if (version == 2018)
    {
        for (R1Keyword& keyword : r1StrictKeywords2018.array)
            keywordTri.Add(keyword.token.str.chars32, keyword.token.str.length, &keyword);
        for (R1Keyword& keyword : r1ReservedKeywords2018.array)
            keywordTri.Add(keyword.token.str.chars32, keyword.token.str.length, &keyword);
    }

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

    if (p)
    {
        path = p;
        if ((err = byteStream.OpenR(p)))
            goto exit;

        unicodeStream.byteStream = &byteStream;

        dynamicToken.reserve(99);

        if ((err = unicodeStream.Get(&currentChar)))
            goto exit;

        while (1)
        {
            R1Keyword* keyword { };
            bool result { };
            err = LexIdentifierOrKeyword(&keyword, &result);
            if (err)
                break;
            if (keyword)
            {
                printf("keyword:'%s'\n", keyword->token.str.chars);
            }
            else
            {
#if _MSC_VER
#pragma warning(push)
#pragma warning(disable:4244) // integer conversion
#endif
                printf("identifier:'%.*s'\n",
                    (int)dynamicToken.size(),
                    std::vector<char>(dynamicToken.begin(), dynamicToken.end()).data());
#if _MSC_VER
#pragma warning(pop)
#endif
            }
//            break;
            // temporary to exercise code
            if (NextChar(&currentChar))
                break;
        }
#if 0
        if (byteStream.position)
        {
            R1Char ch;
            CurrentChar(&ch);
            printf("%c\n", ch);
            NextChar(&ch);
            printf("%c\n", ch);
        }
#endif
    }

exit:
    return err;
}

R1::R1()
{
    tokens.literalByte.token.str.chars = &currentByte;
    tokens.literalByte.token.str.chars32 = &currentChar;
    tokens.literalByte.token.str.length = 1;
    tokens.literalByte.token.type = R1TokenLiteral;

    tokens.literalChar = tokens.literalByte;
    tokens.literalByte.type = R1LiteralByte;
    tokens.literalChar.type = R1LiteralChar;
}

void R1::main(int argc, char** argv)
{
    int i = 1;
    for (; i < argc; ++i)
    {
        if (!argv[i])
            break;
        if (strcmp(argv[i], "--") == 0)
        {
            ++i;
            break;
        }
        else if (strcmp(argv[i], "-2015") == 0)
            version = 2015;
        else if (strcmp(argv[i], "-2018") == 0)
            version = 2018;
        else
            break;
            
    }

//    if (argv[i])
    {
        f1(argv[i]);
    }
}

int main(int argc, char** argv)
{
    R1 r1 { };

#define A(a, b) \
    assert((RoundUpToConstant<unsigned, b>::Value(a) == RoundUpConstantToConstant<unsigned, a, b>::Value)); \
    printf("RoundUpToConstant(%u, %u):%u\n", a, b, RoundUpToConstant<unsigned, b>::Value(a)); \
    printf("RoundUpConstantToConstant(%u, %u):%u\n", a, b, RoundUpConstantToConstant<unsigned, a, b>::Value);
#define B(a) A(a, 10) A(a, 64)
    B(0) B(1) B(2) B(3) B(8) B(9) B(10) B(11) B(12)
    B(60) B(61) B(62) B(63) B(64) B(65)

    r1.main(argc, argv);
}
