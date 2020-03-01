// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#pragma once

void ArenaInitialize();
void ArenaDelete();
void* ArenaAllocate(size_t size);
