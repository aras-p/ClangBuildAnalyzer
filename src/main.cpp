// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "Analysis.h"
#include "BuildEvents.h"
#include "Colors.h"
#include "Utils.h"

#include <stdio.h>
#include <string>
#include <time.h>
#include <map>

#ifdef _MSC_VER
struct IUnknown; // workaround for old Win SDK header failures when using /permissive-
#endif

#define SOKOL_IMPL
#include "external/sokol_time.h"
#define CUTE_FILES_IMPLEMENTATION
#include "external/cute_files.h"

static std::string ReadFileToString(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return "";
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string str;
    str.resize(fsize);
    fread(&str[0], 1, fsize, f);
    fclose(f);
    return str;
}

static bool CompareIgnoreNewlines(const std::string& a, const std::string& b)
{
    size_t alen = a.size();
    size_t blen = b.size();
    size_t ia = 0, ib = 0;
    for (; ia < alen && ib < blen; ++ia, ++ib)
    {
        if (a[ia] == '\r')
            ++ia;
        if (b[ib] == '\r')
            ++ib;
        if (ia < alen && ib < blen)
            if (a[ia] != b[ib])
                return false;
    }
    if (ia != alen || ib != blen)
        return false;
    return true;
}

static void PrintUsage()
{
    printf("%sUSAGE%s: one of\n", col::kBold, col::kReset);
    printf("  ClangBuildAnalyzer %s--start <artifactsdir>%s\n", col::kBold, col::kReset);
    printf("  ClangBuildAnalyzer %s--stop <artifactsdir> <filename>%s\n", col::kBold, col::kReset);
    printf("  ClangBuildAnalyzer %s--analyze <filename>%s\n", col::kBold, col::kReset);
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

#ifdef _MSC_VER
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
    time_t startTime;
    time_t endTime;
    std::map<std::string, std::string> files; // have it sorted by path

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
#ifdef _MSC_VER
        fileModTime = FiletimeToTime(mtime.time);
#else
        fileModTime = mtime.time;
#endif

        if (fileModTime < startTime || fileModTime > endTime)
            return;

        // read the file
        std::string str = ReadFileToString(f->path);
        if (str.empty())
        {
            printf("%s  WARN: could not read file '%s'.%s\n", col::kYellow, f->path, col::kReset);
            return;
        }

        // there might be non-clang time trace json files around;
        // the clang ones should have this inside them
        const char* clangMarker = "{\"cat\":\"\",\"pid\":1,\"tid\":0,\"ts\":0,\"ph\":\"M\",\"name\":\"process_name\",\"args\":{\"name\":\"clang\"}}";
        if (strstr(str.c_str(), clangMarker) == NULL)
            return;

        // do not grab our own merged json file!
        const char* analyzerMarker = "{\"ClangBuildAnalyzerMarker\":\"BigJsonFile\",";
        if (strstr(str.c_str(), analyzerMarker) != NULL)
            return;

        files.insert(std::make_pair(f->path, str));
        //printf("    debug: reading %s\n", f->path);
    }

    static void Callback(cf_file_t* f, void* userData)
    {
        JsonFileFinder* self = (JsonFileFinder*)userData;
        self->OnFile(f);
    }
};

static int RunStop(int argc, const char* argv[])
{
    if (argc < 4)
    {
        printf("%sERROR: --stop requires <artifactsdir> <filename> to be passed.%s\n", col::kRed, col::kReset);
        return 1;
    }

    uint64_t tStart = stm_now();

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

    JsonFileFinder jsonFiles;
    jsonFiles.startTime = startTime;
    jsonFiles.endTime = stopTime;
    cf_traverse(artifactsDir.c_str(), JsonFileFinder::Callback, &jsonFiles);

    if (jsonFiles.files.empty())
    {
        printf("%sERROR: no clang -ftime-trace .json files found under '%s'.%s\n", col::kRed, artifactsDir.c_str(), col::kReset);
        return 1;
    }

    // create a big json file out of all the found ones
    std::string bigJson;
    bigJson.reserve(4*1024*1024);
    bigJson += "{\"ClangBuildAnalyzerMarker\":\"BigJsonFile\",\n";
    bigJson += "\"files\":{\n";
    size_t jsonIndex = 0;
    for (const auto& kvp : jsonFiles.files)
    {
        bigJson += "\"";
        bigJson += kvp.first;
        bigJson += "\":\n";
        bigJson += kvp.second;
        if (jsonIndex != jsonFiles.files.size()-1)
            bigJson += ",";
        bigJson += "\n";
        ++jsonIndex;
    }
    bigJson += "\n}}\n";

    FILE* fout = fopen(outFile.c_str(), "wb");
    if (!fout)
    {
        printf("%sERROR: failed to write result file '%s'.%s\n", col::kRed, outFile.c_str(), col::kReset);
        return 1;
    }
    const size_t numBytesWritten = fwrite(bigJson.data(), 1, bigJson.size(), fout);
    if (numBytesWritten != bigJson.size())
    {
        printf("%sERROR: failed to write result file '%s', %llu of %llu bytes written.%s\n",
            col::kRed, outFile.c_str(), static_cast<unsigned long long>(numBytesWritten), static_cast<unsigned long long>(bigJson.size()), col::kReset);
        fclose(fout);
        return 1;
    }
    fclose(fout);

    double tDuration = stm_sec(stm_since(tStart));
    printf("%s  done in %.1fs. Run 'ClangBuildAnalyzer --analyze %s' to analyze it.%s\n", col::kYellow, tDuration, outFile.c_str(), col::kReset);

    return 0;
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

    // read file
    std::string inFileStr = ReadFileToString(inFile);
    if (inFileStr.empty())
    {
        printf("%sERROR: failed to open file '%s'.%s\n", col::kRed, inFile.c_str(), col::kReset);
        return 1;
    }

    BuildEvents events;
    BuildNames names;
    events.reserve(2048);
    names.reserve(2048);
    ParseBuildEvents(inFileStr, events, names);
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
    std::string traceFile = folder + "/_TraceOutput.json";
    std::string traceExpFile = folder + "/_TraceOutputExpected.json";
    const char* kStopArgs[] =
    {
        "",
        "--stop",
        folder.c_str(),
        traceFile.c_str()
    };
    if (RunStop(4, kStopArgs) != 0)
        return false;

    std::string gotTrace = ReadFileToString(traceFile);
    std::string expTrace = ReadFileToString(traceExpFile);
    if (!CompareIgnoreNewlines(gotTrace, expTrace))
    {
        printf("%sTrace json file (%s) and expected json file (%s) do not match%s\n", col::kRed, traceFile.c_str(), traceExpFile.c_str(), col::kReset);
        return false;
    }

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

    std::string gotAnalysis = ReadFileToString(analyzeFile);
    std::string expAnalysis = ReadFileToString(analyzeExpFile);
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


static int ProcessCommands(int argc, const char* argv[])
{
    if (strcmp(argv[1], "--start") == 0)
        return RunStart(argc, argv);
    if (strcmp(argv[1], "--stop") == 0)
        return RunStop(argc, argv);
    if (strcmp(argv[1], "--analyze") == 0)
        return RunAnalyze(argc, argv, stdout);
    if (strcmp(argv[1], "--test") == 0)
        return RunTests(argc, argv);

    printf("%sUnsupported command line arguments%s\n", col::kRed, col::kReset);
    PrintUsage();
    return 1;
}

int main(int argc, const char* argv[])
{
    col::Initialize();
    utils::Initialize();
    stm_setup();

    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    int retCode = ProcessCommands(argc, argv);

    return retCode;
}
