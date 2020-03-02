// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense

#ifdef _MSC_VER
struct IUnknown; // workaround for old Win SDK header failures when using /permissive-
#endif

#include "Analysis.h"
#include "Arena.h"
#include "Colors.h"
#include "Utils.h"
#include "external/llvm-Demangle/include/Demangle.h"
#include "external/inih/cpp/INIReader.h"
#include <algorithm>
#include <assert.h>
#include <string>
#include <unordered_map>
#include <vector>

struct Config
{
    int fileParseCount = 10;
    int fileCodegenCount = 10;
    int templateCount = 30;
    int functionCount = 30;
    int headerCount = 10;
    int headerChainCount = 5;

    int minFileTime = 10;

    int maxName = 70;

    bool onlyRootHeaders = true;
};

struct pair_hash
{
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2>& p) const
    {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 + 0x9e3779b9 + (h2<<6) + (h2>>2);
    }
};


struct Analysis
{
    Analysis(const BuildEvents& events_, BuildNames& buildNames_, FILE* out_)
    : events(events_)
    , buildNames(buildNames_)
    , out(out_)
    {
        functions.reserve(256);
        instantiations.reserve(256);
        parseFiles.reserve(64);
        codegenFiles.reserve(64);
        headerMap.reserve(256);
    }

    const BuildEvents& events;
    BuildNames& buildNames;

    FILE* out;

    const std::string_view GetBuildName(DetailIndex index)
    {
        assert(index.idx >= 0 && index.idx < buildNames.size());
        return buildNames[index];
    }

    void ProcessEvent(EventIndex eventIndex);
    int largestDetailIndex = 0;
    void EndAnalysis();

    void FindExpensiveHeaders();
    void ReadConfig();

    DetailIndex FindPath(EventIndex eventIndex) const;

    struct InstantiateEntry
    {
        int count = 0;
        int64_t us = 0;
    };
    struct FileEntry
    {
        DetailIndex file;
        int64_t us;
    };
    struct IncludeChain
    {
        std::vector<DetailIndex> files;
        int64_t us = 0;
    };
    struct IncludeEntry
    {
        int64_t us = 0;
        int count = 0;
        bool root = false;
        std::vector<IncludeChain> includePaths;
    };

    std::unordered_map<DetailIndex, std::string_view> collapsedNames;
    std::string_view GetCollapsedName(EventIndex idx);
    void EmitCollapsedTemplates();
    void EmitCollapsedTemplateOpt();
    void EmitCollapsedInfo(
        const std::unordered_map<std::string_view, InstantiateEntry> &collapsed,
        const char *header_string);

    // key is (name,objfile), value is milliseconds
    typedef std::pair<DetailIndex, DetailIndex> IndexPair;
    std::unordered_map<IndexPair, int64_t, pair_hash> functions;
    std::unordered_map<EventIndex, InstantiateEntry> instantiations;
    std::vector<FileEntry> parseFiles;
    std::vector<FileEntry> codegenFiles;
    int64_t totalParseUs = 0;
    int64_t totalCodegenUs = 0;
    int totalParseCount = 0;

    std::unordered_map<std::string_view, IncludeEntry> headerMap;
    std::vector<std::pair<std::string_view, int64_t>> expensiveHeaders;

    Config config;
};

DetailIndex Analysis::FindPath(EventIndex eventIndex) const
{
    while(eventIndex > EventIndex())
    {
        const BuildEvent& ev = events[eventIndex];
        if (ev.type == BuildEventType::kCompiler || ev.type == BuildEventType::kFrontend || ev.type == BuildEventType::kBackend || ev.type == BuildEventType::kOptModule)
            if (ev.detailIndex != DetailIndex())
                return ev.detailIndex;
        eventIndex = ev.parent;
    }
    return DetailIndex();
}

void Analysis::ProcessEvent(EventIndex eventIndex)
{
    const BuildEvent& event = events[eventIndex];
    largestDetailIndex = (std::max)(largestDetailIndex, event.detailIndex.idx);

    if (event.type == BuildEventType::kOptFunction)
    {
        auto funKey = std::make_pair(event.detailIndex, FindPath(eventIndex));
        functions[funKey] += event.dur;
    }

    if (event.type == BuildEventType::kInstantiateClass || event.type == BuildEventType::kInstantiateFunction)
    {
        auto& e = instantiations[eventIndex];
        ++e.count;
        e.us += event.dur;
    }

    if (event.type == BuildEventType::kFrontend)
    {
        totalParseUs += event.dur;
        ++totalParseCount;
        if (event.dur >= config.minFileTime * 1000)
        {
            FileEntry fe;
            fe.file = FindPath(eventIndex);
            fe.us = event.dur;
            parseFiles.emplace_back(fe);
        }
    }
    if (event.type == BuildEventType::kBackend)
    {
        totalCodegenUs += event.dur;
        if (event.dur >= config.minFileTime * 1000)
        {
            FileEntry fe;
            fe.file = FindPath(eventIndex);
            fe.us = event.dur;
            codegenFiles.emplace_back(fe);
        }
    }
    if (event.type == BuildEventType::kParseFile)
    {
        std::string_view path = GetBuildName(event.detailIndex);
        if (utils::IsHeader(path))
        {
            IncludeEntry& e = headerMap[path];
            e.us += event.dur;
            ++e.count;

            // record chain of ParseFile entries leading up to this one
            IncludeChain chain;
            chain.us = event.dur;
            EventIndex parseIndex = event.parent;
            bool hasHeaderBefore = false;
            bool hasNonHeaderBefore = false;
            while(parseIndex.idx >= 0)
            {
                const BuildEvent& ev2 = events[parseIndex];
                if (ev2.type != BuildEventType::kParseFile)
                    break;
                std::string_view ev2path = GetBuildName(ev2.detailIndex);
                chain.files.push_back(ev2.detailIndex);
                bool isHeader = utils::IsHeader(ev2path);
                hasHeaderBefore |= isHeader;
                hasNonHeaderBefore |= !isHeader;
                parseIndex = ev2.parent;
            }

            // only add top-level source path if there was no non-header file down below
            // the include chain (the top-level might be lump/unity file)
            if (!hasNonHeaderBefore)
                chain.files.push_back(FindPath(eventIndex));
            e.root |= !hasHeaderBefore;
            e.includePaths.push_back(chain);
        }
    }
}

static std::string_view CollapseName(const std::string_view& elt)
{
    // Parsing op<, op<<, op>, and op>> seems hard.  Just skip'm all
    if (elt.find("operator") != std::string::npos)
        return elt;

    std::string retval;
    retval.reserve(elt.size());
    auto b_range = elt.begin();
    auto e_range = elt.begin();
    while (b_range != elt.end())
    {
       e_range = std::find(b_range, elt.end(), '<');
        if (e_range == elt.end())
            break;
        ++e_range;
        retval.append(b_range, e_range);
        retval.append("$");
        b_range = e_range;
        int open_count = 1;
        // find the matching close angle bracket
        for (; b_range != elt.end(); ++b_range)
        {
            if (*b_range == '<')
            {
                ++open_count;
                continue;
            }
            if (*b_range == '>')
            {
                if (--open_count == 0)
                {
                    break;
                }
                continue;
            }
        }
        // b_range is now pointing at a close angle, or it is at the end of the string
    }
    if (b_range > e_range)
    {
       // we are in a wacky case where something like op> showed up in a mangled name.
       // just bail.
       // TODO: this still isn't correct, but it avoids crashes.
       return elt;
    }
    // append the footer
    retval.append(b_range, e_range);
    
    size_t size = retval.size();
    char* ptr = (char*)ArenaAllocate(size+1);
    memcpy(ptr, retval.c_str(), size+1);
    return std::string_view(ptr, size);
}

std::string_view Analysis::GetCollapsedName(EventIndex idx)
{
    DetailIndex detail = events[idx].detailIndex;
    std::string_view& name = collapsedNames[detail];
    if (name.empty())
        name = CollapseName(GetBuildName(detail));
    return name;
}

void Analysis::EmitCollapsedInfo(
    const std::unordered_map<std::string_view, InstantiateEntry> &collapsed,
    const char *header_string)
{
    std::vector<std::pair<std::string, InstantiateEntry>> sorted_collapsed;
    sorted_collapsed.resize(std::min<size_t>(config.templateCount, collapsed.size()));
    auto cmp = [](const auto &lhs, const auto &rhs) {
        return std::tie(lhs.second.us, lhs.second.count, lhs.first) > std::tie(rhs.second.us, rhs.second.count, rhs.first);
    };
    std::partial_sort_copy(
        collapsed.begin(), collapsed.end(),
        sorted_collapsed.begin(), sorted_collapsed.end(),
        cmp);

    fprintf(out, "%s%s**** %s%s:\n", col::kBold, col::kMagenta, header_string, col::kReset);
    for (const auto &elt : sorted_collapsed)
    {
        std::string dname = elt.first;
        if (dname.size() > config.maxName)
            dname = dname.substr(0, config.maxName - 2) + "...";
        int ms = int(elt.second.us / 1000);
        int avg = int(ms / elt.second.count);
        fprintf(out, "%s%6i%s ms: %s (%i times, avg %i ms)\n", col::kBold, ms, col::kReset, dname.c_str(), elt.second.count, avg);
    }
    fprintf(out, "\n");
}
void Analysis::EmitCollapsedTemplates()
{
    std::unordered_map<std::string_view, InstantiateEntry> collapsed;
    for (const auto& inst : instantiations)
    {
        const std::string_view name = GetCollapsedName(inst.first);
        auto &stats = collapsed[name];

        bool recursive = false;
        EventIndex p = events[inst.first].parent;
        while (p != EventIndex(-1))
        {
            auto &event = events[p];
            if (event.type == BuildEventType::kInstantiateClass || event.type == BuildEventType::kInstantiateFunction)
            {
                const std::string_view ancestor_name = GetCollapsedName(p);
                if (ancestor_name == name)
                {
                    recursive = true;
                    break;
                }
            }
            p = event.parent;
        }
        if (!recursive)
        {
            stats.us += inst.second.us;
            stats.count += inst.second.count;
        }
    }
    EmitCollapsedInfo(collapsed, "Template sets that took longest to instantiate");
}

void Analysis::EmitCollapsedTemplateOpt()
{
    std::unordered_map<std::string_view, InstantiateEntry> collapsed;
    std::unordered_map<DetailIndex, std::string> collapsedNameCache;
    
    for (const auto& fn : functions)
    {
        auto fnName = fn.first.first;
        auto fnCacheIt = collapsedNameCache.find(fnName);
        if (fnCacheIt == collapsedNameCache.end())
        {
            fnCacheIt = collapsedNameCache.insert(std::make_pair(fnName, CollapseName(llvm::demangle(std::string(GetBuildName(fnName)))))).first;
        }
        auto &stats = collapsed[fnCacheIt->second];
        ++stats.count;
        stats.us += fn.second;
    }
    EmitCollapsedInfo(collapsed, "Function sets that took longest to compile / optimize");
}

void Analysis::EndAnalysis()
{
    if (totalParseUs || totalCodegenUs)
    {
        fprintf(out, "%s%s**** Time summary%s:\n", col::kBold, col::kMagenta, col::kReset);
        fprintf(out, "Compilation (%i times):\n", totalParseCount);
        fprintf(out, "  Parsing (frontend):        %s%7.1f%s s\n", col::kBold, totalParseUs / 1000000.0, col::kReset);
        fprintf(out, "  Codegen & opts (backend):  %s%7.1f%s s\n", col::kBold, totalCodegenUs / 1000000.0, col::kReset);
        fprintf(out, "\n");
    }

    if (!parseFiles.empty())
    {
        std::vector<int> indices;
        indices.resize(parseFiles.size());
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = int(i);
        std::sort(indices.begin(), indices.end(), [&](int indexA, int indexB) {
            const auto& a = parseFiles[indexA];
            const auto& b = parseFiles[indexB];
            if (a.us != b.us)
                return a.us > b.us;
            return a.file < b.file;
            });
        fprintf(out, "%s%s**** Files that took longest to parse (compiler frontend)%s:\n", col::kBold, col::kMagenta, col::kReset);
        for (size_t i = 0, n = std::min<size_t>(config.fileParseCount, indices.size()); i != n; ++i)
        {
            const auto& e = parseFiles[indices[i]];
            fprintf(out, "%s%6i%s ms: %s\n", col::kBold, int(e.us/1000), col::kReset, GetBuildName(e.file).data());
        }
        fprintf(out, "\n");
    }
    if (!codegenFiles.empty())
    {
        std::vector<int> indices;
        indices.resize(codegenFiles.size());
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = int(i);
        std::sort(indices.begin(), indices.end(), [&](int indexA, int indexB) {
            const auto& a = codegenFiles[indexA];
            const auto& b = codegenFiles[indexB];
            if (a.us != b.us)
                return a.us > b.us;
            return a.file < b.file;
            });
        fprintf(out, "%s%s**** Files that took longest to codegen (compiler backend)%s:\n", col::kBold, col::kMagenta, col::kReset);
        for (size_t i = 0, n = std::min<size_t>(config.fileCodegenCount, indices.size()); i != n; ++i)
        {
            const auto& e = codegenFiles[indices[i]];
            fprintf(out, "%s%6i%s ms: %s\n", col::kBold, int(e.us/1000), col::kReset, GetBuildName(e.file).data());
        }
        fprintf(out, "\n");
    }

    if (!instantiations.empty())
    {
        std::vector<std::pair<DetailIndex, InstantiateEntry>> instArray;
        instArray.resize(largestDetailIndex+1);
        for (const auto& inst : instantiations) //collapse the events
        {
            DetailIndex d = events[inst.first].detailIndex;
            instArray[d.idx].first = d;
            instArray[d.idx].second.us += inst.second.us;
            instArray[d.idx].second.count += inst.second.count;
        }
        size_t n = std::min<size_t>(config.templateCount, instArray.size());
        auto cmp = [&](const auto&a, const auto &b) {
            return
                std::tie(a.second.us, a.second.count, a.first) >
                std::tie(b.second.us, b.second.count, b.first);
        };
        std::partial_sort(instArray.begin(), instArray.begin()+n, instArray.end(), cmp);
        fprintf(out, "%s%s**** Templates that took longest to instantiate%s:\n", col::kBold, col::kMagenta, col::kReset);
        for (size_t i = 0; i != n; ++i)
        {
            const auto& e = instArray[i];
            std::string dname = llvm::demangle(std::string(GetBuildName(e.first)));
            if (dname.size() > config.maxName)
                dname = dname.substr(0, config.maxName-2) + "...";
            int ms = int(e.second.us / 1000);
            int avg = int(ms / std::max(e.second.count,1));
            fprintf(out, "%s%6i%s ms: %s (%i times, avg %i ms)\n", col::kBold, ms, col::kReset, dname.c_str(), e.second.count, avg);
        }
        fprintf(out, "\n");

        EmitCollapsedTemplates();
    }

    if (!functions.empty())
    {
        std::vector<std::pair<IndexPair, int64_t>> functionsArray;
        std::vector<int> indices;
        functionsArray.reserve(functions.size());
        indices.reserve(functions.size());
        for (const auto& fn : functions)
        {
            functionsArray.emplace_back(fn);
            indices.emplace_back((int)indices.size());
        }

        std::sort(indices.begin(), indices.end(), [&](int indexA, int indexB) {
            const auto& a = functionsArray[indexA];
            const auto& b = functionsArray[indexB];
            if (a.second != b.second)
                return a.second > b.second;
            return a.first < b.first;
            });
        fprintf(out, "%s%s**** Functions that took longest to compile%s:\n", col::kBold, col::kMagenta, col::kReset);
        for (size_t i = 0, n = std::min<size_t>(config.functionCount, indices.size()); i != n; ++i)
        {
            const auto& e = functionsArray[indices[i]];
            std::string dname = llvm::demangle(std::string(GetBuildName(e.first.first)));
            if (dname.size() > config.maxName)
                dname = dname.substr(0, config.maxName-2) + "...";
            int ms = int(e.second / 1000);
            fprintf(out, "%s%6i%s ms: %s (%s)\n", col::kBold, ms, col::kReset, dname.c_str(), GetBuildName(e.first.second).data());
        }
        fprintf(out, "\n");
        EmitCollapsedTemplateOpt();
    }

    FindExpensiveHeaders();

    if (!expensiveHeaders.empty())
    {
        fprintf(out, "%s%s*** Expensive headers%s:\n", col::kBold, col::kMagenta, col::kReset);
        for (const auto& e : expensiveHeaders)
        {
            const auto& es = headerMap[e.first];
            int ms = int(e.second / 1000);
            int avg = ms / es.count;
            fprintf(out, "%s%i%s ms: %s%s%s (included %i times, avg %i ms), included via:\n", col::kBold, ms, col::kReset, col::kBold, e.first.data(), col::kReset, es.count, avg);
            int pathCount = 0;

            auto sortedIncludeChains = es.includePaths;
            std::sort(sortedIncludeChains.begin(), sortedIncludeChains.end(), [](const auto& a, const auto& b)
            {
                if (a.us != b.us)
                    return a.us > b.us;
                return a.files < b.files;
            });

            for (const auto& chain : sortedIncludeChains)
            {
                fprintf(out, "  ");
                for (auto it = chain.files.rbegin(), itEnd = chain.files.rend(); it != itEnd; ++it)
                {
                    fprintf(out, "%s ", utils::GetFilename(GetBuildName(*it)).data());
                }
                fprintf(out, " (%i ms)\n", int(chain.us/1000));
                ++pathCount;
                if (pathCount > config.headerChainCount)
                    break;
            }
            if (pathCount > config.headerChainCount)
            {
                fprintf(out, "  ...\n");
            }
            fprintf(out, "\n");
        }
    }
}

void Analysis::FindExpensiveHeaders()
{
    expensiveHeaders.reserve(headerMap.size());
    for (const auto& kvp : headerMap)
    {
        if (config.onlyRootHeaders && !kvp.second.root)
            continue;
        expensiveHeaders.push_back(std::make_pair(kvp.first, kvp.second.us));
    }
    std::sort(expensiveHeaders.begin(), expensiveHeaders.end(), [&](const auto& a, const auto& b)
    {
        if (a.second != b.second)
            return a.second > b.second;
        return a.first < b.first;
    });
    if (expensiveHeaders.size() > config.headerCount)
        expensiveHeaders.resize(config.headerCount);
}

void Analysis::ReadConfig()
{
    INIReader ini("ClangBuildAnalyzer.ini");

    config.fileParseCount   = (int)ini.GetInteger("counts", "fileParse",    config.fileParseCount);
    config.fileCodegenCount = (int)ini.GetInteger("counts", "fileCodegen",  config.fileCodegenCount);
    config.functionCount    = (int)ini.GetInteger("counts", "function",     config.functionCount);
    config.templateCount    = (int)ini.GetInteger("counts", "template",     config.templateCount);
    config.headerCount      = (int)ini.GetInteger("counts", "header",       config.headerCount);
    config.headerChainCount = (int)ini.GetInteger("counts", "headerChain",  config.headerChainCount);

    config.minFileTime      = (int)ini.GetInteger("minTimes", "file",       config.minFileTime);

    config.maxName          = (int)ini.GetInteger("misc", "maxNameLength",  config.maxName);
    config.onlyRootHeaders  =      ini.GetBoolean("misc", "onlyRootHeaders",config.onlyRootHeaders);
}


void DoAnalysis(const BuildEvents& events, BuildNames& names, FILE* out)
{
    Analysis a(events, names, out);
    a.ReadConfig();
    for (int i = 0, n = (int)events.size(); i != n; ++i)
        a.ProcessEvent(EventIndex(i));
    a.EndAnalysis();
}
