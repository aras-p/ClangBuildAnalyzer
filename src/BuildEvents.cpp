// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "BuildEvents.h"

#include "Arena.h"
#include "Colors.h"
#include "external/flat_hash_map/bytell_hash_map.hpp"
#include "external/simdjson/simdjson.h"
#include "external/xxHash/xxhash.h"
#include <assert.h>
#include <iterator>

struct HashedString
{
    explicit HashedString(const char* s)
    {
        len = strlen(s);
        hash = XXH64(s, len, 0);
        str = (char*)ArenaAllocate(len+1);
        memcpy(str, s, len+1);
    }
    size_t hash;
    size_t len;
    char* str;
};
namespace std
{
    template<> struct hash<HashedString>
    {
        size_t operator()(const HashedString& v) const
        {
            return v.hash;
        }
    };
    template<> struct equal_to<HashedString>
    {
        bool operator()(const HashedString& a, const HashedString& b) const
        {
            return a.hash == b.hash && a.len == b.len && memcmp(a.str, b.str, a.len) == 0;
        }
    };
} // namespace std

typedef ska::bytell_hash_map<HashedString, DetailIndex> NameToIndexMap;


static void DebugPrintEvents(const BuildEvents& events, const BuildNames& names)
{
    for (size_t i = 0; i < events.size(); ++i)
    {
        const BuildEvent& event = events[EventIndex(int(i))];
        printf("%4zi: t=%i t1=%7llu t2=%7llu par=%4i ch=%4zi det=%s\n", i, event.type, event.ts, event.ts+event.dur, event.parent.idx, event.children.size(), std::string(names[event.detailIndex].substr(0,130)).c_str());
    }
}

static void FindParentChildrenIndices(BuildEvents& events)
{
    if (events.empty())
        return;

    // sort events by start time so that parent events go before child events
    std::vector<EventIndex> sortedIndices;
    sortedIndices.resize(events.size());
    for (int i = 0, n = (int)events.size(); i != n; ++i)
        sortedIndices[i] = EventIndex(i);
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](EventIndex ia, EventIndex ib){
        const auto& ea = events[ia];
        const auto& eb = events[ib];
        if (ea.ts != eb.ts)
            return ea.ts < eb.ts;
        // break start time ties by making longer events go first (they must be parent)
        if (ea.dur != eb.dur)
            return ea.dur > eb.dur;
        // break ties by assuming that later events in sequence must start parent
        return ia > ib;
    });

    // figure out the event hierarchy; for now the parent/child indices are into
    // the "sortedIndices" array and not event indices in the "events" array.
    // As a result, we will be digging into .idx members a lot, as we are temporarily
    // putting the wrong kind of index into 'parent'.
    int root = 0;
    BuildEvent* evRoot = &events[sortedIndices[root]];
    evRoot->parent.idx = -1;
    for (int i = 1, n = (int)events.size(); i != n; ++i)
    {
        BuildEvent* ev2 = &events[sortedIndices[i]];
        while (root != -1)
        {
            // add slice if within bounds
            if (ev2->ts >= evRoot->ts && ev2->ts+ev2->dur <= evRoot->ts+evRoot->dur)
            {
                ev2->parent.idx = root;
                evRoot->children.push_back(EventIndex(i));
                break;
            }
            
            root = evRoot->parent.idx;
            if (root != -1)
                evRoot = &events[sortedIndices[root]];
        }
        if (root == -1)
        {
            ev2->parent.idx = -1;
        }
        root = i;
        evRoot = &events[sortedIndices[i]];
    }

    // fixup event parent/child indices to be into "events" array
    for (auto& e : events)
    {
        for (auto& c : e.children)
            c = sortedIndices[c.idx];
        if (e.parent.idx != -1)
            e.parent = sortedIndices[e.parent.idx];
    }
}

static void AddEvents(BuildEvents& res, BuildEvents& add)
{
    int offset = (int)res.size();
    std::move(add.begin(), add.end(), std::back_inserter(res));
    add.clear();
    for (size_t i = offset, n = res.size(); i != n; ++i)
    {
        BuildEvent& ev = res[EventIndex(int(i))];
        if (ev.parent.idx >= 0)
            ev.parent.idx += offset;
        for (auto& ch : ev.children)
            ch.idx += offset;
    }
}

struct BuildEventsParser
{
    BuildEventsParser()
    {
        NameToIndex(""); // make sure zero index is empty
        resultEvents.reserve(2048);
        resultNames.reserve(2048);
    }

    std::string curFileName;
    BuildEvents resultEvents;
    BuildNames resultNames;
    NameToIndexMap nameToIndex;
    BuildEvents fileEvents;


    DetailIndex NameToIndex(const char* str)
    {
        HashedString hashedName(str);
        auto it = nameToIndex.find(hashedName);
        if (it != nameToIndex.end())
            return it->second;
        DetailIndex index((int)nameToIndex.size());
        nameToIndex.insert(std::make_pair(hashedName, index));
        resultNames.push_back(std::string_view(hashedName.str, hashedName.len));
        return index;
    }

    void ParseRoot(simdjson::document::parser::Iterator& it)
    {
        if (!it.is_object())
            return;
        if (!it.move_to_key("traceEvents"))
            return;
        ParseTraceEvents(it);
    }

    void ParseTraceEvents(simdjson::document::parser::Iterator& it)
    {
        if (!it.is_array())
            return;
        fileEvents.clear();
        if (it.down())
        {
            fileEvents.reserve(256);
            do
            {
                ParseEvent(it);
            } while(it.next());
            it.up();
        }

        FindParentChildrenIndices(fileEvents);
        if (!fileEvents.empty())
        {
            if (fileEvents.back().parent.idx != -1)
            {
                printf("%sERROR: the last trace event should be root; was not in '%s'.%s\n", col::kRed, curFileName.c_str(), col::kReset);
                resultEvents.clear();
                return;
            }
        }
        AddEvents(resultEvents, fileEvents);
    }

    static bool StrEqual(const char* a, const char* b)
    {
        return strcmp(a,b) == 0;
    }

    const char* kPid = "pid";
    const char* kTid = "tid";
    const char* kPh = "ph";
    const char* kName = "name";
    const char* kTs = "ts";
    const char* kDur = "dur";
    const char* kArgs = "args";
    const char* kDetail = "detail";

    void ParseEvent(simdjson::document::parser::Iterator& it)
    {
        if (!it.is_object())
        {
            printf("%sERROR: 'traceEvents' elements in JSON should be objects.%s\n", col::kRed, col::kReset);
            resultEvents.clear();
            return;
        }
        
        if (!it.down())
            return;
        BuildEvent event;
        bool valid = true;
        do
        {
            const char* nodeKey = it.get_string();
            it.next();
            if (StrEqual(nodeKey, kPid))
            {
                if (!it.is_integer() || it.get_integer() != 1)
                    valid = false;
            }
            else if (StrEqual(nodeKey, kTid))
            {
                if (!it.is_integer()) // starting with Clang/LLVM 11 thread IDs are not necessarily 0
                    valid = false;
            }
            else if (StrEqual(nodeKey, kPh))
            {
                if (!it.is_string() || !StrEqual(it.get_string(), "X"))
                    valid = false;
            }
            else if (StrEqual(nodeKey, kName) && it.is_string() && valid)
            {
                const char* name = it.get_string();
                if (!strcmp(name, "ExecuteCompiler"))
                    event.type = BuildEventType::kCompiler;
                else if (!strcmp(name, "Frontend"))
                    event.type = BuildEventType::kFrontend;
                else if (!strcmp(name, "Backend"))
                    event.type = BuildEventType::kBackend;
                else if (!strcmp(name, "Source"))
                    event.type = BuildEventType::kParseFile;
                else if (!strcmp(name, "ParseTemplate"))
                    event.type = BuildEventType::kParseTemplate;
                else if (!strcmp(name, "ParseClass"))
                    event.type = BuildEventType::kParseClass;
                else if (!strcmp(name, "InstantiateClass"))
                    event.type = BuildEventType::kInstantiateClass;
                else if (!strcmp(name, "InstantiateFunction"))
                    event.type = BuildEventType::kInstantiateFunction;
                else if (!strcmp(name, "PerformPendingInstantiations"))
                    ;
                else if (!strcmp(name, "CodeGen Function"))
                    ;
                else if (!strcmp(name, "OptModule"))
                    event.type = BuildEventType::kOptModule;
                else if (!strcmp(name, "OptFunction"))
                    event.type = BuildEventType::kOptFunction;
                else if (!strcmp(name, "PerFunctionPasses") || !strcmp(name, "PerModulePasses") || !strcmp(name, "CodeGenPasses"))
                    ;
                else if (!strcmp(name, "DebugType") || !strcmp(name, "DebugFunction") || !strcmp(name, "DebugGlobalVariable") || !strcmp(name, "DebugConstGlobalVariable"))
                    ;
                else if (!strcmp(name, "RunPass"))
                    ;
                else if (!strcmp(name, "RunLoopPass"))
                    ;
                else if (!strncmp(name, "Total ", 6)) // ignore "Total XYZ" events
                    ;
                else
                {
                    printf("%sWARN: unknown trace event '%s' in '%s', skipping.%s\n", col::kYellow, name, curFileName.c_str(), col::kReset);
                }
            }
            else if (StrEqual(nodeKey, kTs))
            {
                if (it.is_integer())
                    event.ts = it.get_integer();
                else
                    valid = false;
            }
            else if (StrEqual(nodeKey, kDur))
            {
                if (it.is_integer())
                    event.dur = it.get_integer();
                else
                    valid = false;
            }
            else if (StrEqual(nodeKey, kArgs))
            {
                if (it.is_object() && it.down())
                {
                    it.next();
                    if (it.is_string())
                        event.detailIndex = NameToIndex(it.get_string());
                    it.up();
                }
            }
        } while (it.next());
        it.up();
        
        if (event.type== BuildEventType::kUnknown || !valid)
            return;

        if (event.detailIndex == DetailIndex() && event.type == BuildEventType::kCompiler)
            event.detailIndex = NameToIndex(curFileName.c_str());
        fileEvents.emplace_back(event);
    }
};

BuildEventsParser* CreateBuildEventsParser()
{
    BuildEventsParser* p = new BuildEventsParser();
    return p;
}
void DeleteBuildEventsParser(BuildEventsParser* parser)
{
    delete parser;
}


bool ParseBuildEvents(BuildEventsParser* parser, const std::string& fileName, char* jsonText, size_t jsonTextSize)
{
    using namespace simdjson;
    auto [doc, error] = document::parse(get_corpus(fileName));
    if (error)
    {
        printf("%sWARN: JSON parse error %s.%s\n", col::kYellow, error_message(error).c_str(), col::kReset);
        return false;
    }
    
    parser->curFileName = fileName;
    size_t prevEventsSize = parser->resultEvents.size();
    document::parser::Iterator it(doc);
    parser->ParseRoot(it);
    return parser->resultEvents.size() > prevEventsSize;
    //DebugPrintEvents(outEvents, outNames);
}

struct BufferedWriter
{
    BufferedWriter(FILE* f)
    : file(f)
    , size(0)
    {
        
    }
    ~BufferedWriter()
    {
        Flush();
        fclose(file);
    }
    
    template<typename T> void Write(const T& t)
    {
        Write(&t, sizeof(t));
    }
    void Write(const void* ptr, size_t sz)
    {
        if (sz >= kBufferSize)
        {
            fwrite(ptr, sz, 1, file);
            return;
        }
        if (sz + size > kBufferSize)
            Flush();
        memcpy(&buffer[size], ptr, sz);
        size += sz;
    }

    
    void Flush()
    {
        fwrite(buffer, size, 1, file);
        size = 0;
    }
    
    enum { kBufferSize = 65536 };
    uint8_t buffer[kBufferSize];
    int size;
    FILE* file;
};

struct BufferedReader
{
    BufferedReader(FILE* f)
    : pos(0)
    {
        fseek(f, 0, SEEK_END);
        size_t fsize = ftello64(f);
        fseek(f, 0, SEEK_SET);
        buffer = new uint8_t[fsize];
        bufferSize = fsize;
        fread(buffer, bufferSize, 1, f);
        fclose(f);
    }
    ~BufferedReader()
    {
        delete[] buffer;
    }
    
    template<typename T> void Read(T& t)
    {
        Read(&t, sizeof(t));
    }
    void Read(void* ptr, size_t sz)
    {
        if (pos + sz > bufferSize)
        {
            memset(ptr, 0, sz);
            return;
        }
        memcpy(ptr, &buffer[pos], sz);
        pos += sz;
    }
    
    uint8_t* buffer;
    size_t pos;
    size_t bufferSize;
};

bool SaveBuildEvents(BuildEventsParser* parser, const std::string& fileName)
{
    FILE* f = fopen(fileName.c_str(), "wb");
    if (f == nullptr)
    {
        printf("%sERROR: failed to save to file '%s'%s\n", col::kRed, fileName.c_str(), col::kReset);
        return false;
    }
    
    BufferedWriter w(f);

    int64_t eventsCount = parser->resultEvents.size();
    w.Write(eventsCount);
    for(const auto& e : parser->resultEvents)
    {
        int32_t eType = (int32_t)e.type;
        w.Write(eType);
        w.Write(e.ts);
        w.Write(e.dur);
        w.Write(e.detailIndex.idx);
        w.Write(e.parent.idx);
        int64_t childCount = e.children.size();
        w.Write(childCount);
        w.Write(e.children.data(), childCount * sizeof(e.children[0]));
    }

    int64_t namesCount = parser->resultNames.size();
    w.Write(namesCount);
    for(const auto& n : parser->resultNames)
    {
        uint32_t nSize = (uint32_t)n.size();
        w.Write(nSize);
        w.Write(n.data(), nSize);
    }

    return true;
}

bool LoadBuildEvents(const std::string& fileName, BuildEvents& outEvents, BuildNames& outNames)
{
    FILE* f = fopen(fileName.c_str(), "rb");
    if (f == nullptr)
    {
        printf("%sERROR: failed to open file '%s'%s\n", col::kRed, fileName.c_str(), col::kReset);
        return false;
    }
    
    BufferedReader r(f);
    
    int64_t eventsCount = 0;
    r.Read(eventsCount);
    outEvents.resize(eventsCount);
    for(auto& e : outEvents)
    {
        int32_t eType;
        r.Read(eType);
        e.type = (BuildEventType)eType;
        r.Read(e.ts);
        r.Read(e.dur);
        r.Read(e.detailIndex.idx);
        r.Read(e.parent.idx);
        int64_t childCount = 0;
        r.Read(childCount);
        e.children.resize(childCount);
        if (childCount != 0)
            r.Read(&e.children[0], childCount * sizeof(e.children[0]));
    }

    int64_t namesCount = 0;
    r.Read(namesCount);
    outNames.resize(namesCount);
    for(auto& n : outNames)
    {
        uint32_t nSize = 0;
        r.Read(nSize);
        char* ptr = (char*)ArenaAllocate(nSize+1);
        memset(ptr, 0, nSize+1);
        n = std::string_view(ptr, nSize);
        if (nSize != 0)
            r.Read(ptr, nSize);
    }

    fclose(f);

    return true;
}
