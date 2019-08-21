// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "Colors.h"
#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
struct IUnknown; // workaround for old Win SDK header failures when using /permissive-
#include <windows.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#endif

static bool s_UseColors = false;

void col::Initialize()
{
#ifdef _MSC_VER
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	if (GetConsoleMode(hOut, &dwMode))
	{
		s_UseColors = true;

		const int ENABLE_VIRTUAL_TERMINAL_PROCESSING_impl = 0x0004;
		DWORD newMode = dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING_impl;
		if (newMode != dwMode)
			SetConsoleMode(hOut, newMode);
	}
#else // #ifdef _MSC_VER

    if (isatty(fileno(stdout)))
        s_UseColors = true;
    // running from within Xcode pretends the output is a tty, but does not support colors;
    // try to detect Xcode presence via envvar
    if (getenv("__XCODE_BUILT_PRODUCTS_DIR_PATHS") != NULL)
        s_UseColors = false;

#endif // else of #ifdef _MSC_VER

	if (!s_UseColors)
	{
		kBold = kRed = kGreen = kYellow = kBlue = kMagenta = kCyan = kWhite = kReset = "";
	}
}

const char* col::kBold = "\x1B[1m";
const char* col::kRed = "\x1B[91m";
const char* col::kGreen = "\x1B[32m";
const char* col::kYellow = "\x1B[33m";
const char* col::kBlue = "\x1B[34m";
const char* col::kMagenta = "\x1B[35m";
const char* col::kCyan = "\x1B[36m";
const char* col::kWhite = "\x1B[37m";
const char* col::kReset = "\x1B[0m";
