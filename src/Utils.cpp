// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "Utils.h"
#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

static std::string s_CurrentDir;
static std::string s_Root = "./";

inline char ToLower(char c) { return (c >= 'A' && c <= 'Z') ? (c + 'a' - 'A') : c; }
inline char ToUpper(char c) { return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c; }

#ifdef _MSC_VER
std::string WideToUtf(const std::wstring& s)
{
    char path[1000] = { 0 };
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), path, sizeof(path), NULL, NULL);
    return path;
}
#endif


void utils::Initialize()
{
    #ifdef _MSC_VER
	WCHAR buffer[1000] = { 0 };
	GetCurrentDirectoryW(1000, buffer);
	s_CurrentDir = WideToUtf(buffer);
    #else
    s_CurrentDir = getcwd(NULL, 0);
    #endif
	ForwardSlashify(s_CurrentDir);
	if (!s_CurrentDir.empty())
	{ 
		if (*s_CurrentDir.rbegin() != '/')
			s_CurrentDir += '/';
	}
}

void utils::ForwardSlashify(std::string& path)
{
	for (size_t i = 0, n = path.size(); i != n; ++i)
		if (path[i] == '\\')
			path[i] = '/';
}

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

bool utils::EndsWith(const std::string& str, const std::string& suffix)
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


bool utils::IsHeader(const std::string& path)
{
	size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return true; // no extension is likely a header, e.g. <vector>

	size_t len = path.size();
	if (dot + 1 < len && (ToLower(path[dot + 1]) == 'h' || ToLower(path[dot + 1]) == 'i'))
		return true; // extension starting with .h or .i (h, hpp, hxx, inc etc) likely a header

	return false;
}


std::string utils::GetNicePath(const char* path)
{
	const char* ptr = path;
	const char* curDir = s_CurrentDir.c_str();
	while (*ptr && *curDir)
	{
		char c = *ptr;
		char c2 = *curDir;
		if (c == '\\')
			c = '/';
		c = ToLower(c);
		c2 = ToLower(c2);
		if (c != c2)
		{
			std::string res = path;
			utils::ForwardSlashify(res);
			return res;
		}
		++ptr;
		++curDir;
	}
	if (*ptr == 0 && *curDir != 0)
	{
		std::string res = path;
		utils::ForwardSlashify(res);
		return res;
	}
    if (ptr[0] == '.' && ptr[1] == '/')
        ptr += 2;

	std::string res = ptr;
	utils::ForwardSlashify(res);
	return res;
}


std::string utils::GetNicePath(const std::string& path)
{
	std::string res = path;
	ForwardSlashify(res);
	if (BeginsWith(res, s_CurrentDir))
		res = res.substr(s_CurrentDir.size(), res.size() - s_CurrentDir.size());
    if (BeginsWith(res, s_Root))
        res = res.substr(s_Root.size(), res.size() - s_Root.size());
	return res;
}

std::string utils::GetFilename(const std::string& path)
{
	size_t dirIdx = path.rfind('/');
	if (dirIdx != std::string::npos)
		return path.substr(dirIdx + 1, path.size() - dirIdx - 1);
	return path;
}
