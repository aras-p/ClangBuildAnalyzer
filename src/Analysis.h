// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once

#include "BuildEvents.h"
#include <stdio.h>

void DoAnalysis(const BuildEvents& events, BuildNames& names, FILE* out);
