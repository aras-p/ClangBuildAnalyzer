// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once
#include <string>
#include <string_view>

namespace utils
{
    void Initialize();

    std::string GetNicePath(const std::string_view& path);
    std::string_view GetFilename(const std::string_view& path);

    bool IsHeader(const std::string_view& path);

    void Lowercase(std::string& path);
    void ForwardSlashify(std::string& path);

    bool BeginsWith(const std::string& str, const std::string& prefix);
    bool EndsWith(const std::string_view& str, const std::string& suffix);
}
