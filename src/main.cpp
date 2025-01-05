// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include "Analysis.h"
#include "Arena.h"
#include "BuildEvents.h"
#include "Colors.h"
#include "Utils.h"

#include <stdio.h>
#include <string>
#include <time.h>
#include <algorithm>
#include <set>
#include <cassert>

#ifdef _MSC_VER
struct IUnknown; // workaround for old Win SDK header failures when using /permissive-
#endif

static const char* kVersion = "1.6.0";

#include "external/enkiTS/TaskScheduler.h"
#define SOKOL_IMPL
#include "external/sokol_time.h"
#define CUTE_FILES_IMPLEMENTATION
#include "external/cute_files.h"

static void ReadFileToString(const std::string& path, std::string& str)
{
    str.resize(0);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    size_t fsize = ftello64(f);
    fseek(f, 0, SEEK_SET);
    str.resize(fsize);
    fread(&str[0], 1, fsize, f);
    fclose(f);
}

static bool CompareIgnoreNewlines(const std::string& a, const std::string& b)
{
    // Find the next non-newline character in `str`, starting from `idx`.
    //
    // `idx` will be modified. If such a character is found, `true` is returned, 
    // and `idx` will be the index of the found non-newline character.
    //
    // Otherwise, `false` is returned.
    const auto findNextNonNewLineChar = [](const std::string& str, std::size_t& idx)
    {
        for (; idx < str.size(); ++idx)
            if (str[idx] != '\n' && str[idx] != '\r')
                return true;
        
        return false;
    };

    std::size_t idxA = 0;
    std::size_t idxB = 0;

    while (true)
    {
        const bool foundA = findNextNonNewLineChar(a, idxA);
        const bool foundB = findNextNonNewLineChar(b, idxB);
        
        if (!foundA && !foundB) // Reached the end of both files.
            return true;

        if (foundA != foundB) // Mismatch: reached end of only one file.
            return false;

        if (a[idxA] != b[idxB]) // Mismatch: characters do not match.
            return false;

        ++idxA;
        ++idxB;
    }

    assert(false);
    return false;
}

static void PrintUsage()
{
    printf("ClangBuildAnalyzer %s, %sUSAGE%s: one of\n", kVersion, col::kBold, col::kReset);
    printf("  %s--start <artifactsdir>%s\n", col::kBold, col::kReset);
    printf("  %s--stop <artifactsdir> <filename>%s\n", col::kBold, col::kReset);
    printf("  %s--all <artifactsdir> <filename>%s\n", col::kBold, col::kReset);
    printf("  %s--analyze <filename>%s\n", col::kBold, col::kReset);
    printf("  %s--version%s\n", col::kBold, col::kReset);
}

static int RunStart(int argc, const char* argv[])
{
    if (argc < 3)
    {
        printf("%sERROR: --start requires <artifactsdir> to be passed.%s\n", col::kRed, col::kReset);
        return 1;
    }

    std::string artifactsDir = argv[2];
    std::string fname = artifactsDir+"/ClangBuildAnalyzerSession.txt";
    FILE* fsession = fopen(fname.c_str(), "wt");
    if (!fsession)
    {
        printf("%sERROR: failed to create session file at '%s'.%s\n", col::kRed, fname.c_str(), col::kReset);
        return 1;
    }

    // save start timestamp into the session file
    time_t now = time(NULL);
    static_assert(sizeof(time_t)==8, "expected that time_t is a 64-bit number");
#if _MSC_VER
    fprintf(fsession, "%llu\n", now);
#else
    fprintf(fsession, "%lu\n", now);
#endif
    fclose(fsession);

    printf("%sBuild tracing started. Do some Clang builds with '-ftime-trace', then run 'ClangBuildAnalyzer --stop %s <filename>' to stop tracing and save session to a file.%s\n", col::kYellow, artifactsDir.c_str(), col::kReset);

    return 0;
}

#ifdef WIN32
static time_t FiletimeToTime(const FILETIME& ft)
{
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    return ull.QuadPart / 10000000ULL - 11644473600ULL;
}
#endif

struct JsonFileFinder
{
    JsonFileFinder()
    {
    }

    time_t startTime;
    time_t endTime;
    std::vector<std::string> files;

    void OnFile(cf_file_t* f)
    {
        // extension has to be json
        const char* ext = cf_get_ext(f);
        if (ext == NULL || strcmp(ext, ".json") != 0)
            return;

        // modification time between our session start & end
        cf_time_t mtime;
        if (!cf_get_file_time(f->path, &mtime))
            return;
        time_t fileModTime;
#ifdef WIN32
        fileModTime = FiletimeToTime(mtime.time);
#else
        fileModTime = mtime.time;
#endif

        if (fileModTime < startTime || fileModTime > endTime)
            return;

        std::string path = f->path;
        std::replace(path.begin(), path.end(), '\\', '/'); // replace path to forward slashes
        files.emplace_back(path);
    }

    static void Callback(cf_file_t* f, void* userData)
    {
        JsonFileFinder* self = (JsonFileFinder*)userData;
        self->OnFile(f);
    }
};

static int ProcessJsonFiles(const std::string& artifactsDir, const std::string& outFile, time_t startTime, time_t stopTime) {
    uint64_t tStart = stm_now();

   // find .json files with modification times in our interval
    JsonFileFinder jsonFiles;
    jsonFiles.startTime = startTime;
    jsonFiles.endTime = stopTime;
    cf_traverse(artifactsDir.c_str(), JsonFileFinder::Callback, &jsonFiles);
    if (jsonFiles.files.empty())
    {
        printf("%sERROR: no .json files found under '%s'.%s\n", col::kRed, artifactsDir.c_str(), col::kReset);
        return 1;
    }
    // sort input filenames so that runs are deterministic on different file systems
    // with the same input data
    std::sort(jsonFiles.files.begin(), jsonFiles.files.end());

    // parse the json files into our data structures (in parallel)
    BuildEventsParser* parser = CreateBuildEventsParser();
    std::atomic<int> fileCount(0);
    {
        enki::TaskScheduler ts;
        ts.Initialize(std::min(std::thread::hardware_concurrency(), (uint32_t)jsonFiles.files.size()));
        enki::TaskSet task((uint32_t)jsonFiles.files.size(), [&](enki::TaskSetPartition range, uint32_t threadnum)
        {
            for (auto idx = range.start; idx < range.end; ++idx)
            {
                if (ParseBuildEvents(parser, jsonFiles.files[idx]))
                    fileCount++;
            }
        });
        ts.AddTaskSetToPipe(&task);
        ts.WaitforTask(&task);
    }
    if (fileCount == 0)
    {
        printf("%sERROR: no clang -ftime-trace .json files found under '%s'.%s\n", col::kRed, artifactsDir.c_str(), col::kReset);
        return 1;
    }

    // create the data file
    if (!SaveBuildEvents(parser, outFile))
        return 1;

    DeleteBuildEventsParser(parser);
    jsonFiles.files.clear();

    double tDuration = stm_sec(stm_since(tStart));
    printf("%s  done in %.1fs. Run 'ClangBuildAnalyzer --analyze %s' to analyze it.%s\n", col::kYellow, tDuration, outFile.c_str(), col::kReset);

    return 0;
}

static int RunStop(int argc, const char* argv[])
{
    if (argc < 4)
    {
        printf("%sERROR: --stop requires <artifactsdir> <filename> to be passed.%s\n", col::kRed, col::kReset);
        return 1;
    }

    std::string outFile = argv[3];
    printf("%sStopping build tracing and saving to '%s'...%s\n", col::kYellow, outFile.c_str(), col::kReset);

    std::string artifactsDir = argv[2];
    std::string fname = artifactsDir+"/ClangBuildAnalyzerSession.txt";
    FILE* fsession = fopen(fname.c_str(), "rt");
    if (!fsession)
    {
        printf("%sERROR: failed to open session file at '%s'.%s\n", col::kRed, fname.c_str(), col::kReset);
        return 1;
    }

    time_t startTime = 0;
    time_t stopTime = time(NULL);
#if _MSC_VER
    fscanf(fsession, "%llu", &startTime);
#else
    fscanf(fsession, "%lu", &startTime);
#endif
    fclose(fsession);

    return ProcessJsonFiles(artifactsDir, outFile, startTime, stopTime);
}

static int RunAll(int argc, const char* argv[])
{
    if (argc < 4)
    {
        printf("%sERROR: --all requires <artifactsdir> <filename> to be passed.%s\n", col::kRed, col::kReset);
        return 1;
    }

    std::string outFile = argv[3];
    printf("%sProcessing all files and saving to '%s'...%s\n", col::kYellow, outFile.c_str(), col::kReset);

    std::string artifactsDir = argv[2];

    return ProcessJsonFiles(artifactsDir, outFile, 0, std::numeric_limits<time_t>::max());
}

static int RunAnalyze(int argc, const char* argv[], FILE* out)
{
    if (argc < 3)
    {
        printf("%sERROR: --analyze requires <filename> to be passed.%s\n", col::kRed, col::kReset);
        return 1;
    }

    uint64_t tStart = stm_now();

    std::string inFile = argv[2];
    printf("%sAnalyzing build trace from '%s'...%s\n", col::kYellow, inFile.c_str(), col::kReset);

    // load data dump file
    BuildEvents events;
    BuildNames names;
    if (!LoadBuildEvents(inFile, events, names))
        return 1;
    if (events.empty())
    {
        printf("%s  no trace events found.%s\n", col::kYellow, col::kReset);
        return 1;
    }

    DoAnalysis(events, names, out);

    double tDuration = stm_sec(stm_since(tStart));
    printf("%s  done in %.1fs.%s\n", col::kYellow, tDuration, col::kReset);

    return 0;
}

static int RunOneTest(const std::string& folder)
{
    printf("%sRunning test '%s'...%s\n", col::kYellow, folder.c_str(), col::kReset);
    std::string traceFile = folder + "/_TraceOutput.bin";
    const char* kStopArgs[] =
    {
        "",
        "--stop",
        folder.c_str(),
        traceFile.c_str()
    };
    if (RunStop(4, kStopArgs) != 0)
        return false;

    std::string analyzeFile = folder + "/_AnalysisOutput.txt";
    std::string analyzeExpFile = folder + "/_AnalysisOutputExpected.txt";
    const char* kAnalyzeArgs[] =
    {
        "",
        "--analyze",
        traceFile.c_str()
    };
    FILE* out = fopen(analyzeFile.c_str(), "wb");
    if (!out)
    {
        printf("%sFailed to create analysis output file '%s'%s\n", col::kRed, analyzeFile.c_str(), col::kReset);
        return false;
    }
    col::Initialize(true);
    int analysisResult = RunAnalyze(3, kAnalyzeArgs, out);
    col::Initialize();
    fclose(out);
    if (analysisResult != 0)
        return false;

    std::string gotAnalysis, expAnalysis;
    ReadFileToString(analyzeFile, gotAnalysis);
    ReadFileToString(analyzeExpFile, expAnalysis);
    if (!CompareIgnoreNewlines(gotAnalysis, expAnalysis))
    {
        printf("%sAnalysis output (%s) and expected output (%s) do not match%s\n", col::kRed, analyzeFile.c_str(), analyzeExpFile.c_str(), col::kReset);
        printf("--- Got:\n%s\n", gotAnalysis.c_str());
        printf("--- Expected:\n%s\n", expAnalysis.c_str());
        return false;
    }

    return true;
}

static int RunTests(int argc, const char* argv[])
{
    if (argc < 3)
    {
        printf("%sERROR: --test requires <test_folder> to be passed.%s\n", col::kRed, col::kReset);
        return 1;
    }

    uint64_t tStart = stm_now();

    std::string testsFolder = argv[2];
    printf("%sRunning tests under '%s'...%s\n", col::kYellow, testsFolder.c_str(), col::kReset);

    int failures = 0;
    cf_dir_t dir;
    cf_dir_open(&dir, testsFolder.c_str());
    while (dir.has_next)
    {
        cf_file_t entry;
        cf_read_file(&dir, &entry);
        if (entry.is_dir && entry.name[0] != '.')
        {
            if (!RunOneTest(entry.path))
                ++failures;
        }
        cf_dir_next(&dir);
    }
    cf_dir_close(&dir);

    double tDuration = stm_sec(stm_since(tStart));
    printf("%s  tests done in %.1fs.%s\n", col::kYellow, tDuration, col::kReset);
    if (failures != 0)
        printf("%s  had %i failures.%s\n", col::kRed, failures, col::kReset);

    return failures != 0 ? 1 : 0;
}

static int RunVersion(int argc, const char* argv[])
{
    printf("ClangBuildAnalyzer %s%s%s\n", col::kBold, kVersion, col::kReset);
    return 0;
}

static int ProcessCommands(int argc, const char* argv[])
{
    if (strcmp(argv[1], "--start") == 0)
        return RunStart(argc, argv);
    if (strcmp(argv[1], "--stop") == 0)
        return RunStop(argc, argv);
    if (strcmp(argv[1], "--all") == 0)
        return RunAll(argc, argv);
    if (strcmp(argv[1], "--analyze") == 0)
        return RunAnalyze(argc, argv, stdout);
    if (strcmp(argv[1], "--test") == 0)
        return RunTests(argc, argv);
    if (strcmp(argv[1], "--version") == 0)
        return RunVersion(argc, argv);

    printf("%sUnsupported command line arguments%s\n", col::kRed, col::kReset);
    PrintUsage();
    return 1;
}

int main(int argc, const char* argv[])
{
    col::Initialize();
    stm_setup();

    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    ArenaInitialize();
    int retCode = ProcessCommands(argc, argv);
    ArenaDelete();

    return retCode;
}
