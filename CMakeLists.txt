cmake_minimum_required(VERSION 3.5.1)
project(ClangBuildAnalyzer)

add_executable(ClangBuildAnalyzer)
target_sources(ClangBuildAnalyzer PRIVATE
# C sources
    "src/external/inih/ini.c"
# C++ sources	
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
target_compile_features(ClangBuildAnalyzer PRIVATE cxx_std_14)
