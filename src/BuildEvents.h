// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>
#include <utility>


#ifdef _MSC_VER
#define ftello64 _ftelli64
#elif defined(__APPLE__)
#define ftello64 ftello
#endif


enum class BuildEventType
{
    kUnknown,
    kCompiler,
    kFrontend,
    kBackend,
    kParseFile,
    kParseTemplate,
    kParseClass,
    kInstantiateClass,
    kInstantiateFunction,
    kOptModule,
    kOptFunction,
};

struct DetailIndex
{
    int idx;
    explicit DetailIndex(int d = 0) : idx(d) {}
    bool operator==(DetailIndex rhs) const { return idx == rhs.idx; }
    bool operator!=(DetailIndex rhs) const { return idx != rhs.idx; }
    bool operator<(DetailIndex rhs) const { return idx < rhs.idx; }
    bool operator>(DetailIndex rhs) const { return idx > rhs.idx; }
    bool operator<=(DetailIndex rhs) const { return idx <= rhs.idx; }
    bool operator>=(DetailIndex rhs) const { return idx >= rhs.idx; }
};

struct EventIndex
{
    int idx;
    explicit EventIndex(int e = -1) : idx(e) {}
    bool operator==(EventIndex rhs) const { return idx == rhs.idx; }
    bool operator!=(EventIndex rhs) const { return idx != rhs.idx; }
    bool operator<(EventIndex rhs) const { return idx < rhs.idx; }
    bool operator>(EventIndex rhs) const { return idx > rhs.idx; }
    bool operator<=(EventIndex rhs) const { return idx <= rhs.idx; }
    bool operator>=(EventIndex rhs) const { return idx >= rhs.idx; }
};

namespace std
{
    template <> struct hash<DetailIndex>
    {
        size_t operator()(DetailIndex x) const
        {
            return hash<int>()(x.idx);
        }
    };
    template <> struct hash<EventIndex>
    {
        size_t operator()(EventIndex x) const
        {
            return hash<int>()(x.idx);
        }
    };
}

struct BuildEvent
{
    BuildEventType type = BuildEventType::kUnknown;
    int64_t ts = 0;
    int64_t dur = 0;
    DetailIndex detailIndex;
    EventIndex parent{ -1 };
    std::vector<EventIndex> children;
};

template <typename T, typename Idx>
struct IndexedVector : std::vector<T>
{
    using std::vector<T>::vector;
    typename std::vector<T>::reference       operator[](Idx pos) { return this->begin()[pos.idx]; }
    typename std::vector<T>::const_reference operator[](Idx pos) const { return this->begin()[pos.idx]; }
};
typedef IndexedVector<std::string_view, DetailIndex> BuildNames;
typedef IndexedVector<BuildEvent, EventIndex> BuildEvents;

struct BuildEventsParser;
BuildEventsParser* CreateBuildEventsParser();
void DeleteBuildEventsParser(BuildEventsParser* parser);

// NOTE: can be called in parallel
bool ParseBuildEvents(BuildEventsParser* parser, const std::string& fileName);

bool SaveBuildEvents(BuildEventsParser* parser, const std::string& fileName);

bool LoadBuildEvents(const std::string& fileName, BuildEvents& outEvents, BuildNames& outNames);
