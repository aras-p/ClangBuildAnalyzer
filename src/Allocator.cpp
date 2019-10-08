// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include <algorithm>
#include <stdexcept>

#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
struct IUnknown; // workaround for old Win SDK header failures when using /permissive-
#include <windows.h>
#else
#include <stdio.h>
#include <sys/mman.h>
#endif

// Super trivial "allocator" that just never frees memory :)
// Get chunks of memory (32MB each) from OS via VirtualAlloc/mmap,
// allocate by bumping a pointer, and never do any deallocation.
// We are a command line tool, the OS will free up the memory on exit.

const size_t kBlockSize = 32 * 1024 * 1024;
static void* s_CurBlock = nullptr;
static size_t s_CurBlockUsed = 0;
static size_t s_CurBlockSize = 0;


void* operator new(size_t count)
{
    // allocate new chunk of memory if needed
    if (s_CurBlock == nullptr || s_CurBlockUsed + count > s_CurBlockSize)
    {
        s_CurBlockSize = std::max(kBlockSize, count);
#ifdef _MSC_VER
        s_CurBlock = VirtualAlloc(NULL, s_CurBlockSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (s_CurBlock == nullptr)
        {
            DWORD err = GetLastError();
            printf("ERROR: failed to allocate %zu bytes via VirtualAlloc, error %i\n", count, err);
            throw std::bad_alloc();
            return nullptr;
        }
#else
        s_CurBlock = mmap(0, s_CurBlockSize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
        if (s_CurBlock == nullptr)
        {
            printf("ERROR: failed to allocate %zu bytes via mmap\n", count);
            throw std::bad_alloc();
        }
#endif
        s_CurBlockUsed = 0;
    }

    void* res = (char*)s_CurBlock + s_CurBlockUsed;
    s_CurBlockUsed += count;
    return res;
}

void operator delete(void * p) throw()
{
}
