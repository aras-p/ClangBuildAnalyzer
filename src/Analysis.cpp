#include "Analysis.h"
#include "Colors.h"
#include "Utils.h"
#include <algorithm>
#include <assert.h>
#include <string>
#include <unordered_map>
#include <vector>

struct Config
{
	int fileParseCount = 10;
	int fileCodegenCount = 10;
	int functionCount = 30;
	int headerCount = 5;
	int headerChainCount = 10;

	int minFileTime = 10;
	int minFunctionTime = 3;

	int maxFunctionName = 70;

	bool onlyRootHeaders = true;
};

struct Analysis
{
	Analysis()
	{
		functions.reserve(256);
		parseFiles.reserve(64);
		codegenFiles.reserve(64);
		headerMap.reserve(256);
	}

	void ProcessEvent(const BuildEvents& events, int eventIndex);
	void EndAnalysis();

	void FindExpensiveHeaders();
	void ReadConfig();

	struct FunctionEntry
	{
		std::string name;
		std::string obj;
		int ms;
	};
	struct FileEntry
	{
		std::string file;
		int ms;
	};
	struct IncludeChain
	{
		std::vector<std::string> files;
		int ms = 0;
	};
	struct IncludeEntry
	{
		int ms = 0;
		int count = 0;
		bool root = false;
		std::vector<IncludeChain> includePaths;
	};


	std::vector<FunctionEntry> functions;
	std::vector<FileEntry> parseFiles;
	std::vector<FileEntry> codegenFiles;
	int totalParseMs = 0;
	int totalCodegenMs = 0;
	int totalParseCount = 0;

	std::unordered_map<std::string, IncludeEntry> headerMap;
	std::vector<std::pair<std::string, int>> expensiveHeaders;

	Config config;
};

static std::string FindPath(const BuildEvents& events, int eventIndex)
{
    while(eventIndex >= 0)
    {
        const BuildEvent& ev = events[eventIndex];
        if (ev.type == BuildEventType::kCompiler || ev.type == BuildEventType::kFrontend || ev.type == BuildEventType::kBackend || ev.type == BuildEventType::kOptModule)
            if (!ev.detail.empty())
                return utils::GetNicePath(ev.detail);
        eventIndex = ev.parent;
    }
    return "<unknown>";
}

void Analysis::ProcessEvent(const BuildEvents& events, int eventIndex)
{
    const BuildEvent& event = events[eventIndex];
    const int ms = int(event.dur / 1000);

	if (event.type == BuildEventType::kOptFunction)
	{
        if (ms >= config.minFunctionTime)
        {
            FunctionEntry fe;
            fe.name = event.detail;
            fe.ms = ms;
            fe.obj = FindPath(events, eventIndex);
            functions.emplace_back(fe);
        }
	}

	if (event.type == BuildEventType::kFrontend)
	{
		totalParseMs += ms;
		++totalParseCount;
		if (ms >= config.minFileTime)
		{
			FileEntry fe;
            fe.file = FindPath(events, eventIndex);
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
            fe.file = FindPath(events, eventIndex);
            fe.ms = ms;
            codegenFiles.emplace_back(fe);
		}
	}
	if (event.type == BuildEventType::kParseFile)
	{
        std::string path = utils::GetNicePath(event.detail);

		IncludeEntry& e = headerMap[path];
		e.ms += ms;
		++e.count;

		// record chain of ParseFile entries leading up to this one
        IncludeChain chain;
        int parseIndex = event.parent;
        bool hasHeaderBeforeIt = false;
        while(parseIndex >= 0)
        {
            const BuildEvent& ev2 = events[parseIndex];
            if (ev2.type != BuildEventType::kParseFile)
                break;
            std::string path = utils::GetNicePath(ev2.detail);
            chain.files.push_back(path);
            //chain.ms += //@TODO add exclusive duration
            //@TODO hasHeaderBeforeIt |= utils::IsHeader(path);
            parseIndex = ev2.parent;
        }
        
        if (!chain.files.empty())
        {
            e.root |= !hasHeaderBeforeIt;
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
			printf("%s%6i%s ms: %s\n", col::kBold, e.ms, col::kReset, e.file.c_str());
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
			printf("%s%6i%s ms: %s\n", col::kBold, e.ms, col::kReset, e.file.c_str());
		}
		printf("\n");
	}

	if (!functions.empty())
	{
		std::vector<int> indices;
		indices.resize(functions.size());
		for (size_t i = 0; i < functions.size(); ++i)
			indices[i] = int(i);
		std::sort(indices.begin(), indices.end(), [&](int indexA, int indexB) {
			const auto& a = functions[indexA];
			const auto& b = functions[indexB];
			return a.ms > b.ms;
			});
		printf("%s%s**** Functions that took longest to compile%s:\n", col::kBold, col::kMagenta, col::kReset);
		for (size_t i = 0, n = std::min<size_t>(config.functionCount, indices.size()); i != n; ++i)
		{
			const auto& e = functions[indices[i]];
            std::string dname = e.name; //@TODO demangler::Demangle(e.name);
			if (dname.size() > config.maxFunctionName)
				dname = dname.substr(0, config.maxFunctionName-2) + "...";
			printf("%s%6i%s ms: %s (%s)\n", col::kBold, e.ms, col::kReset, dname.c_str(), e.obj.c_str());
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
			printf("-%s%6i%s ms: %s%s%s (included %i times, avg %i ms), included via:\n", col::kBold, e.second, col::kReset, col::kBold, e.first.c_str(), col::kReset, es.count, e.second / es.count);
			int pathCount = 0;

			auto sortedIncludeChains = es.includePaths;
			std::sort(sortedIncludeChains.begin(), sortedIncludeChains.end(), [](const auto& a, const auto& b) { return a.ms > b.ms; });

			for (const auto& chain : sortedIncludeChains)
			{
				printf("  ");
                for (auto it = chain.files.rbegin(), itEnd = chain.files.rend(); it != itEnd; ++it)
				{
                    printf("%s ", utils::GetFilename(*it).c_str());
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
		if (kvp.second.includePaths.empty())
			continue; // not included by anything (just a source file)
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
    /* //@TODO
	INIReader ini("ClangBuildAnalyzer.ini");

	config.fileParseCount	= ini.GetInteger("counts", "fileParse",		config.fileParseCount);
	config.fileCodegenCount = ini.GetInteger("counts", "fileCodegen",	config.fileCodegenCount);
	config.functionCount	= ini.GetInteger("counts", "function",		config.functionCount);
	config.headerCount		= ini.GetInteger("counts", "header",		config.headerCount);
	config.headerChainCount = ini.GetInteger("counts", "headerChain",	config.headerChainCount);

	config.minFileTime		= ini.GetInteger("minTimes", "file",		config.minFileTime);
	config.minFunctionTime	= ini.GetInteger("minTimes", "function",	config.minFunctionTime);

	config.maxFunctionName	= ini.GetInteger("misc", "maxFunctionLength",	config.maxFunctionName);
	config.onlyRootHeaders	= ini.GetBoolean("misc", "onlyRootHeaders",		config.onlyRootHeaders);
     */
}


void DoAnalysis(const BuildEvents& events)
{
    Analysis a;
    a.ReadConfig();
    for (int i = 0, n = (int)events.size(); i != n; ++i)
        a.ProcessEvent(events, i);
    a.EndAnalysis();
}
