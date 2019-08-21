# Clang Build Analyzer [![Build Status](https://api.travis-ci.org/aras-p/ClangBuildAnalyzer.svg?branch=master)](https://travis-ci.org/aras-p/ClangBuildAnalyzer)

Clang C/C++ build analysis tool when using Clang 9 `-ftime-trace`. The `-ftime-trace` compiler flag
(see [Aras' blog post](https://aras-p.info/blog/2019/01/16/time-trace-timeline-flame-chart-profiler-for-Clang/)) can be useful
to figure out what takes time during compilation of *one* source file. This tool helps to aggregate time trace
reports from multiple compilations, and output "what took the most time" summary:

- Which files are slowest to parse? i.e. spend time in compiler lexer/parser front-end
- Which C++ templates took the most time to instantiate?
- Which files are slowest to generate code for? i.e. spend time in compiler backend doing codegen and optimizations
- Which functions are slowest to generate code for?
- Which header files are included the most in the whole build, how much time is spent parsing them, and what are the include chains of them?


### Usage

1. **Start the build capture**: `ClangBuildAnalyzer --start <artifacts_folder>`<br/>
   This will write current timestamp in a `ClangBuildAnalyzerSession.txt` file under the given `artifacts_folder`. The artifacts
   folder is where the compiled object files (and time trace report files) are expected to be produced by your build.
1. **Do your build**. Does not matter how; an IDE build, a makefile, a shell script, whatever. As long as it invokes
   Clang and passes `-ftime-trace` flag to the compiler (**Clang 9.0 or later is required** for this).
1. **Stop the build capture**: `ClangBuildAnalyzer --stop <artifacts_folder> <capture_file>`<br/>
   This will find all Clang time trace compatible `*.json` files under the given `artifacts_folder` that were modified after
   `--start` step was done (Clang `-ftime-trace` produces one JSON file next to each object file), and smashes them together into
   one big `capture_file`.
1. **Run the build analysis**: `ClangBuildAnalyzer --analyze <capture_file>`<br/>
   This will read the `capture_file` produced by `--stop` step, calculate the slowest things and print them. If a
   `ClangBuildAnalyzer.ini` file exists in the current folder, it will be read to control how many of various things to print.


### Analysis Output

The analysis output will look something like this:

```
Analyzing build trace from 'artifacts/FullCapture.json'...
**** Time summary:
Compilation (1761 times):
  Parsing (frontend):         5167.4 s
  Codegen & opts (backend):   7576.5 s

**** Files that took longest to parse (compiler frontend):
 19524 ms: artifacts/Modules_TLS_0.o
 18046 ms: artifacts/Editor_Src_4.o
 17026 ms: artifacts/Modules_Audio_Public_1.o
 16581 ms: artifacts/Runtime_Camera_4.o
 
**** Files that took longest to codegen (compiler backend):
145761 ms: artifacts/Modules_TLS_0.o
123048 ms: artifacts/Runtime_Core_Containers_1.o
 56975 ms: artifacts/Runtime_Testing_3.o
 52031 ms: artifacts/Tools_ShaderCompiler_1.o

**** Templates that took longest to instantiate:
 19006 ms: std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::... (2665 times, avg 7 ms)
 12821 ms: std::__1::map<core::basic_string<char, core::StringStorageDefault<ch... (250 times, avg 51 ms)
  9142 ms: std::__1::map<core::basic_string<char, core::StringStorageDefault<ch... (432 times, avg 21 ms)
  8515 ms: std::__1::map<int, std::__1::pair<List<ListNode<Behaviour> > *, List... (392 times, avg 21 ms) 

**** Functions that took longest to compile:
  8710 ms: yyparse(glslang::TParseContext*) (External/ShaderCompilers/glslang/glslang/MachineIndependent/glslang_tab.cpp)
  4580 ms: LZ4HC_compress_generic_dictCtx (External/Compression/lz4/lz4hc_quarantined.c)
  4011 ms: sqlite3VdbeExec (External/sqlite/sqlite3.c)
  2737 ms: ProgressiveRuntimeManager::Update() (artifacts/Editor_Src_GI_Progressive_0.cpp)

*** Expensive headers:
136567 ms: /BuildEnvironment/MacOSX10.14.sdk/System/Library/Frameworks/Foundation.framework/Headers/Foundation.h (included 92 times, avg 1484 ms), included via:
  CocoaObjectImages.o AppKit.h  (2033 ms)
  OSXNativeWebViewWindowHelper.o OSXNativeWebViewWindowHelper.h AppKit.h  (2007 ms)
  RenderSurfaceMetal.o RenderSurfaceMetal.h MetalSupport.h Metal.h MTLTypes.h  (2003 ms)
  OSXWebViewWindowPrivate.o AppKit.h  (1959 ms)
  ...

112344 ms: Runtime/BaseClasses/BaseObject.h (included 729 times, avg 154 ms), included via:
  PairTests.cpp TestFixtures.h  (337 ms)
  Stacktrace.cpp MonoManager.h GameManager.h EditorExtension.h  (312 ms)
  PlayerPrefs.o PlayerSettings.h GameManager.h EditorExtension.h  (301 ms)
  Animation.cpp MaterialDescription.h  (299 ms)
  ...

103856 ms: Runtime/Threads/ReadWriteLock.h (included 478 times, avg 217 ms), included via:
  DownloadHandlerAssetBundle.cpp AssetBundleManager.h  (486 ms)
  LocalizationDatabase.cpp LocalizationDatabase.h LocalizationAsset.h StringTable.h  (439 ms)
  Runtime_BaseClasses_1.o MonoUtility.h ScriptingProfiler.h  (418 ms)
  ...
```

Granularity and amount of most expensive things (files, functions, templates, includes) that are reported can be controlled by having an
`ClangBuildAnalyzer.ini` file in the working directory. Take a look at [`ClangBuildAnalyzer.ini`](/ClangBuildAnalyzer.ini) for an example.


### Building it

* Windows: Visual Studio 2017 solution at `projects/vs2017/ClangBuildAnalyzer.sln`.
* Mac: Xcode 9.x project at `projects/xcode/ClangBuildAnalyzer.xcodeproj`.
* Linux: Makefile for gcc (tested with 5.4), build with `make -f projects/make/Makefile`.


### Limitations

* Does not capture anything related to linking (or LTO, I guess) right now.
* I haven't tried running it on _huge_ builds; largest I ran was several thousand compiler invocations; and
  the analysis step runs in about 10 seconds on that. I haven't tried on something ginormous like a Chrome build;
  I expect some of my lazy code might not scale to that (I do have one `O(N^2)` place... yeah yeah, I know, shame on
  me).


### License

License for the Clang Build Analyzer itself is [Unlicense](https://unlicense.org/), i.e. public domain. However, the source code
includes several external library source files (all under `src/external`), each with their own license:

* `cute_files.h` from [RandyGaul/cute_headers](https://github.com/RandyGaul/cute_headers): zlib or public domain,
* `inih`, from [benhoyt/inih](https://github.com/benhoyt/inih): BSD 3 clause,
* `llvm-Demangle`, part of [LLVM](https://llvm.org/): Apache-2.0 with LLVM-exception,
* `sajson.h` from [chadaustin/sajson](https://github.com/chadaustin/sajson): MIT,
* `sokol_time.h` from [floooh/sokol](https://github.com/floooh/sokol): zlib/libpng.
