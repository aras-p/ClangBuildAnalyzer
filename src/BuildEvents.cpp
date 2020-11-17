// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "BuildEvents.h"

#include "Arena.h"
#include "Colors.h"
#include "Utils.h"
#include "external/cute_files.h"
#include "external/flat_hash_map/bytell_hash_map.hpp"
#include "external/llvm-Demangle/include/Demangle.h"
#include "external/simdjson/simdjson.h"
#include "external/xxHash/xxhash.h"
#include <assert.h>
#include <iterator>
#include <mutex>

struct HashedString
{
    explicit HashedString(const char* s)
    {
        len = strlen(s);
        hash = XXH64(s, len, 0);
        str = s;
    }
    size_t hash;
    size_t len;
    const char* str;
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
        printf("%4zi: t=%i t1=%7ld t2=%7ld par=%4i ch=%4zi det=%s\n", i, (int) event.type, event.ts, event.ts+event.dur, event.parent.idx, event.children.size(), std::string(names[event.detailIndex].substr(0,130)).c_str());
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
    
#ifndef NDEBUG
    for (int i = 0, n = (int)events.size(); i != n; ++i)
    {
        assert(i != events[EventIndex(i)].parent.idx);
    }
#endif
}

struct BuildEventsParser
{
    BuildEventsParser()
    {
        // make sure zero index is empty
        NameToIndex("", resultNameToIndex);
        resultNames.push_back(std::string_view(resultNameToIndex.begin()->first.str, 0));
        
        resultEvents.reserve(2048);
        resultNames.reserve(2048);
    }

    BuildEvents resultEvents;
    BuildNames resultNames;
    NameToIndexMap resultNameToIndex;
    std::mutex resultMutex;
    std::mutex arenaMutex;
    
    void AddEvents(BuildEvents& add, const NameToIndexMap& nameToIndex)
    {
        // we got job-local build events and name-to-index mapping;
        // add them to the global result with any necessary remapping.
        // gotta take a mutex since we're modifying shared state here.
        std::scoped_lock lock(resultMutex);
        
        // move events to end of result events list
        int offset = (int)resultEvents.size();
        std::move(add.begin(), add.end(), std::back_inserter(resultEvents));
        add.clear();
        
        // create remapping from name indices, adding them to global remapping
        // list if necessary.
        ska::bytell_hash_map<DetailIndex, DetailIndex> detailRemap;
        for (const auto& kvp : nameToIndex)
        {
            const auto& existing = resultNameToIndex.find(kvp.first);
            if (existing == resultNameToIndex.end())
            {
                DetailIndex index((int)resultNameToIndex.size());
                resultNameToIndex.insert(std::make_pair(kvp.first, index));
                resultNames.push_back(std::string_view(kvp.first.str, kvp.first.len));
                detailRemap[kvp.second] = index;
            }
            else
            {
                detailRemap[kvp.second] = existing->second;
            }
        }
        
        // adjust the added event indices
        for (size_t i = offset, n = resultEvents.size(); i != n; ++i)
        {
            BuildEvent& ev = resultEvents[EventIndex(int(i))];
            if (ev.parent.idx >= 0)
                ev.parent.idx += offset;
            for (auto& ch : ev.children)
                ch.idx += offset;
            if (ev.detailIndex.idx != 0)
            {
                assert(ev.detailIndex.idx >= 0 && ev.detailIndex.idx < nameToIndex.size());
                ev.detailIndex = detailRemap[ev.detailIndex];
                assert(ev.detailIndex.idx >= 0 && ev.detailIndex.idx < resultNameToIndex.size());
            }
        }
        
        assert(resultNameToIndex.size() == resultNames.size());
    }


    DetailIndex NameToIndex(const char* str, NameToIndexMap& nameToIndex)
    {
        HashedString hashedName(str);
        auto it = nameToIndex.find(hashedName);
        if (it != nameToIndex.end())
            return it->second;

        char* strCopy;
        {
            // arena allocator is not thread safe, take a mutex
            std::scoped_lock lock(arenaMutex);
            strCopy = (char*)ArenaAllocate(hashedName.len+1);
        }
        memcpy(strCopy, str, hashedName.len+1);
        hashedName.str = strCopy;

        DetailIndex index((int)nameToIndex.size());
        nameToIndex.insert(std::make_pair(hashedName, index));
        return index;
    }

    bool ParseRoot(simdjson::dom::element& it, const std::string& curFileName)
    {
        simdjson::dom::element nit;
        if (it["traceEvents"].get(nit))
            return false;
        return ParseTraceEvents(nit, curFileName);
    }

    bool ParseTraceEvents(simdjson::dom::element& it, const std::string& curFileName)
    {
        if (!it.is_array())
            return false;

        NameToIndexMap nameToIndexLocal;
        NameToIndex("", nameToIndexLocal); // make sure zero index is empty
        BuildEvents fileEvents;
        fileEvents.reserve(256);
        for (simdjson::dom::element nit : it)
        {
            ParseEvent(nit, curFileName, fileEvents, nameToIndexLocal);
        }
        if (fileEvents.empty())
            return false;

        FindParentChildrenIndices(fileEvents);
        if (fileEvents.back().parent.idx != -1)
        {
            printf("%sWARN: the last trace event should be root; was not in '%s'.%s\n", col::kRed, curFileName.c_str(), col::kReset);
            return false;
        }
        AddEvents(fileEvents, nameToIndexLocal);
        return true;
    }

    static bool StrEqual(std::string_view a, const char* b)
    {
        return a == b;
    }

    static bool StartsWith(std::string_view a, const char* b, int blen)
    {
        return a.size() >= blen && a.compare(0, blen, b) == 0;
    }

    const char* kPid = "pid";
    const char* kTid = "tid";
    const char* kPh = "ph";
    const char* kName = "name";
    const char* kTs = "ts";
    const char* kDur = "dur";
    const char* kArgs = "args";
    const char* kDetail = "detail";

    void ParseEvent(simdjson::dom::element& it, const std::string& curFileName, BuildEvents& fileEvents, NameToIndexMap& nameToIndexLocal)
    {
        simdjson::dom::object node;
        if (it.get(node))
        {
            printf("%sERROR: 'traceEvents' elements in JSON should be objects.%s\n", col::kRed, col::kReset);
            resultEvents.clear();
            return;
        }
        
        BuildEvent event;
        bool valid = true;
        std::string_view detailPtr;
        for (simdjson::dom::key_value_pair kv : node)
        {
            std::string_view nodeKey = kv.key;
            if (StrEqual(nodeKey, kPid))
            {
                if (!kv.value.is_int64())  // starting with Clang/LLVM 11 process IDs are not necessarily 1
                    valid = false;
            }
            else if (StrEqual(nodeKey, kTid))
            {
                if (!kv.value.is_int64()) // starting with Clang/LLVM 11 thread IDs are not necessarily 0
                    valid = false;
            }
            else if (StrEqual(nodeKey, kPh))
            {
                if (!kv.value.is_string() || !StrEqual(kv.value.get_string(), "X"))
                    valid = false;
            }
            else if (StrEqual(nodeKey, kName) && kv.value.is_string() && valid)
            {
                std::string_view name = kv.value.get_string();
                if (StrEqual(name, "ExecuteCompiler"))
                    event.type = BuildEventType::kCompiler;
                else if (StrEqual(name, "Frontend"))
                    event.type = BuildEventType::kFrontend;
                else if (StrEqual(name, "Backend"))
                    event.type = BuildEventType::kBackend;
                else if (StrEqual(name, "Source"))
                    event.type = BuildEventType::kParseFile;
                else if (StrEqual(name, "ParseTemplate"))
                    event.type = BuildEventType::kParseTemplate;
                else if (StrEqual(name, "ParseClass"))
                    event.type = BuildEventType::kParseClass;
                else if (StrEqual(name, "InstantiateClass"))
                    event.type = BuildEventType::kInstantiateClass;
                else if (StrEqual(name, "InstantiateFunction"))
                    event.type = BuildEventType::kInstantiateFunction;
                else if (StrEqual(name, "PerformPendingInstantiations"))
                    ;
                else if (StrEqual(name, "CodeGen Function"))
                    ;
                else if (StrEqual(name, "OptModule"))
                    event.type = BuildEventType::kOptModule;
                else if (StrEqual(name, "OptFunction"))
                    event.type = BuildEventType::kOptFunction;
                else if (StrEqual(name, "PerFunctionPasses") || StrEqual(name, "PerModulePasses") || StrEqual(name, "CodeGenPasses"))
                    ;
                else if (StrEqual(name, "DebugType") || StrEqual(name, "DebugFunction") || StrEqual(name, "DebugGlobalVariable") || StrEqual(name, "DebugConstGlobalVariable"))
                    ;
                else if (StrEqual(name, "RunPass"))
                    ;
                else if (StrEqual(name, "RunLoopPass"))
                    ;
                else if (StartsWith(name, "Total ", 6)) // ignore "Total XYZ" events
                    ;
                else
                {
                    printf("%sWARN: unknown trace event '%s' in '%s', skipping.%s\n", col::kYellow, std::string(name).c_str(), curFileName.c_str(), col::kReset);
                }
            }
            else if (StrEqual(nodeKey, kTs))
            {
                if (kv.value.is_int64())
                    event.ts = kv.value.get_int64();
                else
                    valid = false;
            }
            else if (StrEqual(nodeKey, kDur))
            {
                if (kv.value.is_int64())
                    event.dur = kv.value.get_int64();
                else
                    valid = false;
            }
            else if (StrEqual(nodeKey, kArgs))
            {
                if (kv.value.is_object())
                {
                    simdjson::dom::object kvo(kv.value);
                    simdjson::dom::key_value_pair args = *kvo.begin();
                    if (args.value.is_string())
                        detailPtr = args.value.get_string();
                }
            }
        };
        
        if (event.type== BuildEventType::kUnknown || !valid)
            return;

        // if the "compiler" event has no detail name, use the current json file name
        if (detailPtr.empty() && event.type == BuildEventType::kCompiler)
            detailPtr = curFileName;
        if (!detailPtr.empty())
        {
            std::string detailString;
            if (event.type == BuildEventType::kParseFile || event.type == BuildEventType::kOptModule)
            {
                // do various cleanups/nice-ifications of the detail name:
                // make paths shorter (i.e. relative to project) where possible
                detailString = utils::GetNicePath(detailPtr);
            }
            else
                detailString = detailPtr;

            // don't report the clang trace .json file, instead get the object file at the same location if it's there
            if (utils::EndsWith(detailString, ".json"))
            {
                std::string candidate = std::string(detailString.substr(0, detailString.length()-4)) + "o";
                // check for .o
                if (cf_file_exists(candidate.c_str()))
                    detailString = candidate;
                else
                {
                    // check for .obj
                    candidate += "bj";
                    if (cf_file_exists(candidate.c_str()))
                        detailString = candidate;
                }
            }
            // demangle possibly mangled names
            if (event.type == BuildEventType::kOptFunction || event.type == BuildEventType::kInstantiateClass || event.type == BuildEventType::kInstantiateFunction)
                detailString = llvm::demangle(detailString);

            event.detailIndex = NameToIndex(detailString.c_str(), nameToIndexLocal);
        }

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


bool ParseBuildEvents(BuildEventsParser* parser, const std::string& fileName)
{
    using namespace simdjson;
    dom::parser p;
    dom::element doc;
    auto error = p.load(fileName).get(doc);
    if (error)
    {
        printf("%sWARN: JSON parse error %s.%s\n", col::kYellow, error_message(error), col::kReset);
        return false;
    }
    
    return parser->ParseRoot(doc, fileName);
    //DebugPrintEvents(outEvents, outNames);
}

struct BufferedWriter
{
    BufferedWriter(FILE* f)
    : file(f)
    , size(0)
    {
        hasher = XXH64_createState();
        XXH64_reset(hasher, 0);
    }
    ~BufferedWriter()
    {
        Flush();
        XXH64_hash_t hash = XXH64_digest(hasher);
        fwrite(&hash, sizeof(hash), 1, file);
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
            if( size > 0 )
            {
                Flush();
            }

            XXH64_update(hasher, ptr, sz);
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
        XXH64_update(hasher, buffer, size);
        size = 0;
    }
    
    enum { kBufferSize = 65536 };
    uint8_t buffer[kBufferSize];
    size_t size;
    FILE* file;
    XXH64_state_t* hasher;
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

const uint32_t kFileMagic = 'CBA0';

bool SaveBuildEvents(BuildEventsParser* parser, const std::string& fileName)
{
    FILE* f = fopen(fileName.c_str(), "wb");
    if (f == nullptr)
    {
        printf("%sERROR: failed to save to file '%s'%s\n", col::kRed, fileName.c_str(), col::kReset);
        return false;
    }
    
    BufferedWriter w(f);

    w.Write(kFileMagic);
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
    if (r.bufferSize < 12) // 4 bytes magic header, 8 bytes hash at end
    {
        printf("%sERROR: corrupt input file '%s' (size too small)%s\n", col::kRed, fileName.c_str(), col::kReset);
        return false;
    }
    // check header magic
    int32_t magic = 0;
    r.Read(magic);
    if (magic != kFileMagic)
    {
        printf("%sERROR: unknown format of input file '%s'%s\n", col::kRed, fileName.c_str(), col::kReset);
        return false;
    }
    // chech hash checksum
    XXH64_hash_t hash = XXH64(r.buffer, r.bufferSize-sizeof(XXH64_hash_t), 0);
    if (memcmp(&hash, r.buffer+r.bufferSize-sizeof(XXH64_hash_t), sizeof(XXH64_hash_t)) != 0)
    {
        printf("%sERROR: corrupt input file '%s' (checksum mismatch)%s\n", col::kRed, fileName.c_str(), col::kReset);
        return false;
    }

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

    return true;
}
