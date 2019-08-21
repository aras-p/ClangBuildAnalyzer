// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once

namespace col
{

    void Initialize(bool forceDisableColors = false);

    extern const char* kBold;
    extern const char* kRed;
    extern const char* kGreen;
    extern const char* kYellow;
    extern const char* kBlue;
    extern const char* kMagenta;
    extern const char* kCyan;
    extern const char* kWhite;
    extern const char* kReset;
}
