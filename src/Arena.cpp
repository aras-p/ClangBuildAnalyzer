// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense

#include <cstdint>
#include <algorithm>
#include <vector>

struct ArenaBlock
{
    uint8_t* buffer;
    size_t bufferSize;
    size_t used;
};

static std::vector<ArenaBlock> s_Blocks;

const size_t kDefaultBlockSize = 65536;


void ArenaInitialize()
{
}

void ArenaDelete()
{
    for (auto& b : s_Blocks)
        delete[] b.buffer;
    s_Blocks.clear();
}

void* ArenaAllocate(size_t size)
{
    // do we need a new block?
    if (s_Blocks.empty() || s_Blocks.back().used + size > s_Blocks.back().bufferSize)
    {
        ArenaBlock block;
        block.bufferSize = std::max(size, kDefaultBlockSize);
        block.buffer = new uint8_t[block.bufferSize];
        block.used = 0;
        s_Blocks.emplace_back(block);
    }
    
    // allocate from the last block
    ArenaBlock& b = s_Blocks.back();
    void* ptr = b.buffer + b.used;
    b.used += size;
    return ptr;
}

