# Clang Build Analyzer [![Build Status](https://github.com/aras-p/ClangBuildAnalyzer/workflows/build_and_test/badge.svg)](https://github.com/aras-p/ClangBuildAnalyzer/actions)

Clang C/C++ build analysis tool when using Clang 9+ `-ftime-trace`. The `-ftime-trace` compiler flag
(see [blog post](https://aras-p.info/blog/2019/01/16/time-trace-timeline-flame-chart-profiler-for-Clang/) or
[Clang 9 release notes](https://releases.llvm.org/9.0.0/tools/clang/docs/ReleaseNotes.html#new-compiler-flags)) can be useful
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
   This will load all Clang time trace compatible `*.json` files under the given `artifacts_folder` that were modified after
   `--start` step was done (Clang `-ftime-trace` produces one JSON file next to each object file), process them and store data file into
   a binary `capture_file`.
1. **Run the build analysis**: `ClangBuildAnalyzer --analyze <capture_file>`<br/>
   This will read the `capture_file` produced by `--stop` step, calculate the slowest things and print them. If a
   `ClangBuildAnalyzer.ini` file exists in the current folder, it will be read to control how many of various things to print.

Aternatively, instead of doing `--start` and `--stop` steps, you can do `ClangBuildAnalyzer --all <artifacts_folder> <capture_file>` after your build; that will
include all the compatible `*.json` files for analysis, no matter when they were produced.


### Analysis Output

The analysis output will look something like this:

```
Analyzing build trace from 'artifacts/FullCapture.bin'...
**** Time summary:
Compilation (7664 times):
  Parsing (frontend):         2118.9 s
  Codegen & opts (backend):   1204.1 s

**** Files that took longest to parse (compiler frontend):
  5084 ms: cycles_scene.build/RelWithDebInfo/volume.o
  4471 ms: extern_ceres.build/RelWithDebInfo/covariance_impl.o
  4225 ms: bf_intern_libmv.build/RelWithDebInfo/resect.o
  4121 ms: bf_blenkernel.build/RelWithDebInfo/volume_to_mesh.o
 
**** Files that took longest to codegen (compiler backend):
 47123 ms: bf_blenkernel.build/RelWithDebInfo/volume.o
 39617 ms: bf_blenkernel.build/RelWithDebInfo/volume_to_mesh.o
 37488 ms: bf_modifiers.build/RelWithDebInfo/MOD_volume_displace.o
 30676 ms: bf_gpu.build/RelWithDebInfo/gpu_shader_create_info.o

**** Templates that took longest to instantiate:
 11172 ms: fmt::detail::vformat_to<char> (142 times, avg 78 ms)
  6662 ms: std::__scalar_hash<std::_PairT, 2>::operator() (3549 times, avg 1 ms)
  6281 ms: std::__murmur2_or_cityhash<unsigned long, 64>::operator() (3549 times, avg 1 ms)
  5757 ms: std::basic_string<char>::basic_string (3597 times, avg 1 ms)
  5541 ms: blender::CPPType::to_static_type_tag<float, blender::VecBase<float, ... (70 times, avg 79 ms)

**** Template sets that took longest to instantiate:
 32421 ms: std::unique_ptr<$> (30461 times, avg 1 ms)
 30098 ms: Eigen::MatrixBase<$> (8639 times, avg 3 ms)
 27524 ms: Eigen::internal::call_assignment_no_alias<$> (2397 times, avg 11 ms)

**** Functions that took longest to compile:
 28359 ms: gpu_shader_create_info_init (source/blender/gpu/intern/gpu_shader_create_info.cc)
  4090 ms: ccl::GetConstantValues(ccl::KernelData const*) (intern/cycles/device/metal/kernel.mm)
  3996 ms: gpu_shader_dependency_init (source/blender/gpu/intern/gpu_shader_dependency.cc)

**** Function sets that took longest to compile / optimize:
 10606 ms: bool openvdb::v10_0::tree::NodeList<$>::initNodeChildren<$>(openvdb:... (470 times, avg 22 ms)
  9640 ms: void tbb::interface9::internal::dynamic_grainsize_mode<$>::work_bala... (919 times, avg 10 ms)
  9459 ms: void tbb::interface9::internal::dynamic_grainsize_mode<$>::work_bala... (715 times, avg 13 ms)
  7279 ms: blender::Vector<$>::realloc_to_at_least(long long) (1840 times, avg 3 ms)
 
**** Expensive headers:
261580 ms: /Developer/SDKs/MacOSX13.1.sdk/usr/include/c++/v1/algorithm (included 3389 times, avg 77 ms), included via:
  341x: BKE_context.h BLI_string_ref.hh string 
  180x: DNA_mesh_types.h BLI_math_vector_types.hh array 
  125x: DNA_space_types.h DNA_node_types.h DNA_node_tree_interface_types.h BLI_function_ref.hh BLI_memory_utils.hh 
  ...

188777 ms: /Developer/SDKs/MacOSX13.1.sdk/usr/include/c++/v1/string (included 3447 times, avg 54 ms), included via:
  353x: BKE_context.h BLI_string_ref.hh 
  184x: DNA_mesh_types.h BLI_offset_indices.hh BLI_index_mask.hh BLI_linear_allocator.hh BLI_string_ref.hh 
  131x: DNA_node_types.h DNA_node_tree_interface_types.h BLI_span.hh 
  ...

174792 ms: source/blender/makesdna/DNA_node_types.h (included 1653 times, avg 105 ms), included via:
  316x: ED_screen.hh DNA_space_types.h 
  181x: DNA_space_types.h 
  173x: <direct include>
  ...
```

Granularity and amount of most expensive things (files, functions, templates, includes) that are reported can be controlled by having an
`ClangBuildAnalyzer.ini` file in the working directory. Take a look at [`ClangBuildAnalyzer.ini`](/ClangBuildAnalyzer.ini) for an example.


### Building it

* Windows: Visual Studio 2019 solution at `projects/vs2019/ClangBuildAnalyzer.sln`.
* Mac: Xcode 10.x project at `projects/xcode/ClangBuildAnalyzer.xcodeproj`.
* Linux: Makefile for gcc (tested with 7.4), build with `make -f projects/make/Makefile`.
* You can also use provided `CMakeLists.txt`, if you want to build using `CMake`.

### Limitations

* Does not capture anything related to linking or LTO right now.
* May or may not scale to _huge_ builds (I haven't tried on something ginormous like a Chrome
  build). However I have tried it on Unity editor and Blender builds and it worked fine.


### License

License for the Clang Build Analyzer itself is [Unlicense](https://unlicense.org/), i.e. public domain. However, the source code
includes several external library source files (all under `src/external`), each with their own license:

* `cute_files.h` from [RandyGaul/cute_headers](https://github.com/RandyGaul/cute_headers): zlib or public domain,
* `cwalk` from [likle/cwalk](https://github.com/likle/cwalk): MIT,
* `enkiTS`, from [dougbinks/enkiTS](https://github.com/dougbinks/enkiTS): zlib, version 058150d2 (2024 Apr),
* `flat_hash_map`, from [skarupke/flat_hash_map](https://github.com/skarupke/flat_hash_map): Boost 1.0,
* `inih`, from [benhoyt/inih](https://github.com/benhoyt/inih): BSD 3 clause,
* `llvm-Demangle`, part of [LLVM](https://llvm.org/): Apache-2.0 with LLVM-exception,
* `simdjson` from [lemire/simdjson](https://github.com/lemire/simdjson): Apache-2.0, version 3.10.1 (2024 Aug),
* `sokol_time.h` from [floooh/sokol](https://github.com/floooh/sokol): zlib/libpng,
* `xxHash` from [Cyan4973/xxHash](https://github.com/Cyan4973/xxHash): BSD 2 clause, version 0.8.2 (2023 Jul).
