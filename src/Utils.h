// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once
#include <string>
#include <string_view>

namespace utils
{
    [[nodiscard]] std::string GetNicePath(const std::string_view& path);
    [[nodiscard]] std::string_view GetFilename(const std::string_view& path);

    [[nodiscard]] bool IsHeader(std::string_view path);

    void Lowercase(std::string& path);

    [[nodiscard]] bool BeginsWith(const std::string& str, const std::string& prefix);
    [[nodiscard]] bool EndsWith(const std::string_view& str, const std::string& suffix);
}
