cmake_minimum_required(VERSION 3.5.1)
project(ClangBuildAnalyzer)

string(APPEND CMAKE_CXX_FLAGS " -std=c++14 -O2")

set(C_SRCS
    "src/external/inih/ini.c"
)

set(CPP_SRCS
    "src/Allocator.cpp"
    "src/Analysis.cpp"
    "src/BuildEvents.cpp"
    "src/Colors.cpp"
    "src/main.cpp"
    "src/Utils.cpp"
    "src/external/inih/cpp/INIReader.cpp"
    "src/external/llvm-Demangle/lib/Demangle.cpp"
    "src/external/llvm-Demangle/lib/ItaniumDemangle.cpp"
    "src/external/llvm-Demangle/lib/MicrosoftDemangle.cpp"
    "src/external/llvm-Demangle/lib/MicrosoftDemangleNodes.cpp"
)

add_executable(ClangBuildAnalyzer ${C_SRCS} ${CPP_SRCS})
