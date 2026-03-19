cmake_minimum_required(VERSION 3.5.1)
project(ClangBuildAnalyzer)

include(GNUInstallDirs)

set(EXTERNAL_SRC
# C sources
    "src/external/cwalk/cwalk.c"
    "src/external/inih/ini.c"
    "src/external/xxHash/xxhash.c"
# C++ sources
    "src/external/enkiTS/TaskScheduler.cpp"
    "src/external/inih/cpp/INIReader.cpp"
    "src/external/llvm-Demangle/lib/Demangle.cpp"
    "src/external/llvm-Demangle/lib/ItaniumDemangle.cpp"
    "src/external/llvm-Demangle/lib/MicrosoftDemangle.cpp"
    "src/external/llvm-Demangle/lib/MicrosoftDemangleNodes.cpp"
    "src/external/simdjson/simdjson.cpp"
)

set(SRC
# C++ sources
    "src/Analysis.cpp"
    "src/Arena.cpp"
    "src/BuildEvents.cpp"
    "src/Colors.cpp"
    "src/main.cpp"
    "src/Utils.cpp"
)

set(DISABLE_WARNINGS_FLAG "")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang|Intel") # Check C++ compiler first
    set(DISABLE_WARNINGS_FLAG "-w")
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang|Intel") # Fallback for C-only
    set(DISABLE_WARNINGS_FLAG "-w")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(DISABLE_WARNINGS_FLAG "/w") # MSVC uses /w
elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set(DISABLE_WARNINGS_FLAG "/w")
endif()

set_source_files_properties(${EXTERNAL_SRC}
    PROPERTIES COMPILE_FLAGS "${DISABLE_WARNINGS_FLAG}"
)

add_executable(ClangBuildAnalyzer "${EXTERNAL_SRC}" "${SRC}")
target_compile_features(ClangBuildAnalyzer PRIVATE cxx_std_17)

find_library(LIBRT rt)
if(LIBRT)
    target_link_libraries(ClangBuildAnalyzer ${LIBRT})
endif()

find_package(Threads REQUIRED)
target_link_libraries(ClangBuildAnalyzer Threads::Threads)

install(TARGETS ClangBuildAnalyzer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
