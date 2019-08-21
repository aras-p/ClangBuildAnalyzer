// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense

#ifdef _MSC_VER
struct IUnknown; // workaround for old Win SDK header failures when using /permissive-
#endif

#include "Analysis.h"
#include "Colors.h"
#include "Utils.h"
#include "external/llvm-Demangle/include/Demangle.h"
#include "external/inih/cpp/INIReader.h"
#include "external/cute_files.h"
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
	Analysis(const BuildEvents& events_, BuildNames& buildNames_)
    : events(events_)
    , buildNames(buildNames_)
    , buildNamesDone(buildNames_.size(), 0)
	{
        buildNamesDone.resize(buildNames.size());
		functions.reserve(256);
        instantiations.reserve(256);
		parseFiles.reserve(64);
		codegenFiles.reserve(64);
		headerMap.reserve(256);
	}
    
    const BuildEvents& events;
    BuildNames& buildNames;
    std::vector<char> buildNamesDone;
    
    const std::string& GetBuildName(int index)
    {
        auto& name = buildNames[index];
        if (!buildNamesDone[index])
        {
            name = utils::GetNicePath(name);
            // don't report the clang trace .json file, instead get the object file at the same location if it's there
            if (utils::EndsWith(name, ".json"))
            {
                std::string candidate = name.substr(0, name.length()-4) + "o";
                if (cf_file_exists(candidate.c_str()))
                    name = candidate;
				else
				{
					candidate += "bj";
					if (cf_file_exists(candidate.c_str()))
						name = candidate;
				}
			}
            buildNamesDone[index] = 1;
        }
        return name;
    }

	void ProcessEvent(int eventIndex);
	void EndAnalysis();

	void FindExpensiveHeaders();
	void ReadConfig();
    
    int FindPath(int eventIndex) const;

    struct InstantiateEntry
    {
        int count;
        int ms;
    };
	struct FileEntry
	{
		int file;
		int ms;
	};
	struct IncludeChain
	{
		std::vector<int> files;
		int ms = 0;
	};
	struct IncludeEntry
	{
		int ms = 0;
		int count = 0;
		bool root = false;
		std::vector<IncludeChain> includePaths;
	};


    // key is (name,objfile), value is milliseconds
    typedef std::pair<int,int> IndexPair;
	std::unordered_map<IndexPair, int, pair_hash> functions;
    std::unordered_map<int, InstantiateEntry> instantiations;
	std::vector<FileEntry> parseFiles;
	std::vector<FileEntry> codegenFiles;
	int totalParseMs = 0;
	int totalCodegenMs = 0;
	int totalParseCount = 0;

	std::unordered_map<std::string, IncludeEntry> headerMap;
	std::vector<std::pair<std::string, int>> expensiveHeaders;

	Config config;
};

int Analysis::FindPath(int eventIndex) const
{
    while(eventIndex >= 0)
    {
        const BuildEvent& ev = events[eventIndex];
        if (ev.type == BuildEventType::kCompiler || ev.type == BuildEventType::kFrontend || ev.type == BuildEventType::kBackend || ev.type == BuildEventType::kOptModule)
            if (ev.detailIndex != 0)
                return ev.detailIndex;
        eventIndex = ev.parent;
    }
    return 0;
}

void Analysis::ProcessEvent(int eventIndex)
{
    const BuildEvent& event = events[eventIndex];
    const int ms = int(event.dur / 1000);

	if (event.type == BuildEventType::kOptFunction)
	{
        auto funKey = std::make_pair(event.detailIndex, FindPath(eventIndex));
        functions[funKey] += ms;
	}
    
    if (event.type == BuildEventType::kInstantiateClass || event.type == BuildEventType::kInstantiateFunction)
    {
        auto& e = instantiations[event.detailIndex];
        ++e.count;
        e.ms += ms;
    }

	if (event.type == BuildEventType::kFrontend)
	{
		totalParseMs += ms;
		++totalParseCount;
		if (ms >= config.minFileTime)
		{
			FileEntry fe;
            fe.file = FindPath(eventIndex);
			fe.ms = ms;
			parseFiles.emplace_back(fe);
		}
	}
	if (event.type == BuildEventType::kBackend)
	{
		totalCodegenMs += ms;
		if (ms >= config.minFileTime)
		{
            FileEntry fe;
            fe.file = FindPath(eventIndex);
            fe.ms = ms;
            codegenFiles.emplace_back(fe);
		}
	}
	if (event.type == BuildEventType::kParseFile)
	{
        std::string path = GetBuildName(event.detailIndex);
        if (utils::IsHeader(path))
        {
            IncludeEntry& e = headerMap[path];
            e.ms += ms;
            ++e.count;

            // record chain of ParseFile entries leading up to this one
            IncludeChain chain;
            chain.ms = ms;
            int parseIndex = event.parent;
            bool hasHeaderBefore = false;
            bool hasNonHeaderBefore = false;
            while(parseIndex >= 0)
            {
                const BuildEvent& ev2 = events[parseIndex];
                if (ev2.type != BuildEventType::kParseFile)
                    break;
                std::string ev2path = GetBuildName(ev2.detailIndex);
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

void Analysis::EndAnalysis()
{
	if (totalParseMs || totalCodegenMs)
	{
		printf("%s%s**** Time summary%s:\n", col::kBold, col::kMagenta, col::kReset);
		printf("Compilation (%i times):\n", totalParseCount);
		printf("  Parsing (frontend):        %s%7.1f%s s\n", col::kBold, totalParseMs / 1000.0, col::kReset);
		printf("  Codegen & opts (backend):  %s%7.1f%s s\n", col::kBold, totalCodegenMs / 1000.0, col::kReset);
		printf("\n");
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
			return a.ms > b.ms;
			});
		printf("%s%s**** Files that took longest to parse (compiler frontend)%s:\n", col::kBold, col::kMagenta, col::kReset);
		for (size_t i = 0, n = std::min<size_t>(config.fileParseCount, indices.size()); i != n; ++i)
		{
			const auto& e = parseFiles[indices[i]];
			printf("%s%6i%s ms: %s\n", col::kBold, e.ms, col::kReset, GetBuildName(e.file).c_str());
		}
		printf("\n");
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
			return a.ms > b.ms;
			});
		printf("%s%s**** Files that took longest to codegen (compiler backend)%s:\n", col::kBold, col::kMagenta, col::kReset);
		for (size_t i = 0, n = std::min<size_t>(config.fileCodegenCount, indices.size()); i != n; ++i)
		{
			const auto& e = codegenFiles[indices[i]];
			printf("%s%6i%s ms: %s\n", col::kBold, e.ms, col::kReset, GetBuildName(e.file).c_str());
		}
		printf("\n");
	}

    if (!instantiations.empty())
    {
        std::vector<std::pair<int, InstantiateEntry>> instArray;
        std::vector<int> indices;
        instArray.reserve(instantiations.size());
        indices.reserve(instantiations.size());
        for (const auto& inst : instantiations)
        {
            instArray.emplace_back(inst);
            indices.emplace_back((int)indices.size());
        }
        
        std::sort(indices.begin(), indices.end(), [&](int indexA, int indexB) {
            const auto& a = instArray[indexA];
            const auto& b = instArray[indexB];
            return a.second.ms > b.second.ms;
        });
        printf("%s%s**** Templates that took longest to instantiate%s:\n", col::kBold, col::kMagenta, col::kReset);
        for (size_t i = 0, n = std::min<size_t>(config.templateCount, indices.size()); i != n; ++i)
        {
            const auto& e = instArray[indices[i]];
            std::string dname = llvm::demangle(GetBuildName(e.first));
            if (dname.size() > config.maxName)
                dname = dname.substr(0, config.maxName-2) + "...";
            printf("%s%6i%s ms: %s (%i times, avg %i ms)\n", col::kBold, e.second.ms, col::kReset, dname.c_str(), e.second.count, e.second.ms/e.second.count);
        }
        printf("\n");
    }

    if (!functions.empty())
	{
        std::vector<std::pair<IndexPair, int>> functionsArray;
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
			return a.second > b.second;
			});
		printf("%s%s**** Functions that took longest to compile%s:\n", col::kBold, col::kMagenta, col::kReset);
		for (size_t i = 0, n = std::min<size_t>(config.functionCount, indices.size()); i != n; ++i)
		{
			const auto& e = functionsArray[indices[i]];
            std::string dname = llvm::demangle(GetBuildName(e.first.first));
			if (dname.size() > config.maxName)
				dname = dname.substr(0, config.maxName-2) + "...";
			printf("%s%6i%s ms: %s (%s)\n", col::kBold, e.second, col::kReset, dname.c_str(), GetBuildName(e.first.second).c_str());
		}
		printf("\n");
	}

	FindExpensiveHeaders();

	if (!expensiveHeaders.empty())
	{
		printf("%s%s*** Expensive headers%s:\n", col::kBold, col::kMagenta, col::kReset);
		for (const auto& e : expensiveHeaders)
		{
			const auto& es = headerMap[e.first];
			printf("%s%i%s ms: %s%s%s (included %i times, avg %i ms), included via:\n", col::kBold, e.second, col::kReset, col::kBold, e.first.c_str(), col::kReset, es.count, e.second / es.count);
			int pathCount = 0;

			auto sortedIncludeChains = es.includePaths;
			std::sort(sortedIncludeChains.begin(), sortedIncludeChains.end(), [](const auto& a, const auto& b) { return a.ms > b.ms; });

			for (const auto& chain : sortedIncludeChains)
			{
				printf("  ");
                for (auto it = chain.files.rbegin(), itEnd = chain.files.rend(); it != itEnd; ++it)
				{
                    printf("%s ", utils::GetFilename(GetBuildName(*it)).c_str());
				}
				printf(" (%i ms)\n", chain.ms);
				++pathCount;
				if (pathCount > config.headerChainCount)
					break;
			}
			if (pathCount > config.headerChainCount)
			{
				printf("  ...\n");
			}
			printf("\n");
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
		expensiveHeaders.push_back(std::make_pair(kvp.first, kvp.second.ms));
	}
	std::sort(expensiveHeaders.begin(), expensiveHeaders.end(), [&](const auto& a, const auto& b) { return a.second > b.second; });
	if (expensiveHeaders.size() > config.headerCount)
		expensiveHeaders.resize(config.headerCount);
}

void Analysis::ReadConfig()
{
	INIReader ini("ClangBuildAnalyzer.ini");

	config.fileParseCount	= (int)ini.GetInteger("counts", "fileParse",	config.fileParseCount);
	config.fileCodegenCount = (int)ini.GetInteger("counts", "fileCodegen",	config.fileCodegenCount);
	config.functionCount	= (int)ini.GetInteger("counts", "function",		config.functionCount);
    config.templateCount    = (int)ini.GetInteger("counts", "template",     config.templateCount);
	config.headerCount		= (int)ini.GetInteger("counts", "header",		config.headerCount);
	config.headerChainCount = (int)ini.GetInteger("counts", "headerChain",	config.headerChainCount);

	config.minFileTime		= (int)ini.GetInteger("minTimes", "file",		config.minFileTime);

	config.maxName	        = (int)ini.GetInteger("misc", "maxNameLength",  config.maxName);
	config.onlyRootHeaders	=      ini.GetBoolean("misc", "onlyRootHeaders",config.onlyRootHeaders);
}


void DoAnalysis(const BuildEvents& events, BuildNames& names)
{
    Analysis a(events, names);
    a.ReadConfig();
    for (int i = 0, n = (int)events.size(); i != n; ++i)
        a.ProcessEvent(i);
    a.EndAnalysis();
}
