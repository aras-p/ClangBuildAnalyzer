// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "Utils.h"

#include "external/cwalk/cwalk.h"
#include <string.h>

inline char ToLower(char c) { return (c >= 'A' && c <= 'Z') ? (c + 'a' - 'A') : c; }
inline char ToUpper(char c) { return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c; }

void utils::Lowercase(std::string& path)
{
    for (size_t i = 0, n = path.size(); i != n; ++i)
        path[i] = ToLower(path[i]);
}


bool utils::BeginsWith(const std::string& str, const std::string& prefix)
{
    if (str.size() < prefix.size())
        return false;
    for (size_t i = 0, n = prefix.size(); i != n; ++i)
    {
        char c1 = ToLower(str[i]);
        char c2 = ToLower(prefix[i]);
        if (c1 != c2)
            return false;
    }
    return true;
}

bool utils::EndsWith(const std::string_view& str, const std::string& suffix)
{
    if (str.size() < suffix.size())
        return false;
    size_t start = str.size() - suffix.size();
    for (size_t i = 0, n = suffix.size(); i != n; ++i)
    {
        char c1 = ToLower(str[i+start]);
        char c2 = ToLower(suffix[i]);
        if (c1 != c2)
            return false;
    }
    return true;
}


bool utils::IsHeader(const std::string_view& path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return true; // no extension is likely a header, e.g. <vector>

    size_t len = path.size();
    if (dot + 1 < len && (ToLower(path[dot + 1]) == 'h' || ToLower(path[dot + 1]) == 'i'))
        return true; // extension starting with .h or .i (h, hpp, hxx, inc etc) likely a header

    return false;
}

std::string utils::GetNicePath(const std::string_view& path)
{
    char input[FILENAME_MAX];
    size_t len = std::min<size_t>(path.size(), FILENAME_MAX-1);
    memcpy(input, path.data(), len);
    input[len] = 0;
    char result[FILENAME_MAX];
    cwk_path_normalize(input, result, sizeof(result));
    // convert to forward slashes
    char *p = result;
    while (*p)
    {
      if (*p == '\\')
          *p = '/';
      ++p;
    }
    return result;
}

std::string_view utils::GetFilename(const std::string_view& path)
{
    size_t dirIdx = path.rfind('/');
    if (dirIdx != std::string::npos)
        return path.substr(dirIdx + 1, path.size() - dirIdx - 1);
    return path;
}
