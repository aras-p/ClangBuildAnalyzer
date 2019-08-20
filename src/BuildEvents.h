// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

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

struct BuildEvent
{
    BuildEventType type = BuildEventType::kUnknown;
    uint64_t ts = 0;
    uint64_t dur = 0;
    std::string detail;
    int parent = -1;
    std::vector<int> children;
};

typedef std::vector<BuildEvent> BuildEvents;

BuildEvents ParseBuildEvents(std::string& jsonText);
