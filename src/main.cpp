// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "Analysis.h"
#include "BuildEvents.h"
#include "Colors.h"
#include "Utils.h"

#include <stdio.h>
#include <string>
#include <time.h>
#include <unordered_map>

#define SOKOL_IMPL
#include "external/sokol_time.h"
#define CUTE_FILES_IMPLEMENTATION
#include "external/cute_files.h"

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
    fprintf(fsession, "%lu\n", now);
    fclose(fsession);

    printf("%sBuild tracing started. Do some Clang builds with '-ftime-trace', then run 'ClangBuildAnalyzer --stop %s <filename>' to stop tracing and save session to a file.%s\n", col::kYellow, artifactsDir.c_str(), col::kReset);

    return 0;
}

struct JsonFileFinder
{
    time_t startTime;
    time_t endTime;
    std::unordered_map<std::string, std::string> files;

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
        if (mtime.time < startTime || mtime.time > endTime)
            return;
        
        // read the file
        FILE* ff = fopen(f->path, "rb");
        if (!ff)
        {
            printf("%s  WARN: could not read file '%s'.%s\n", col::kYellow, f->path, col::kReset);
            return;
        }
        fseek(ff, 0, SEEK_END);
        size_t fsize = ftell(ff);
        fseek(ff, 0, SEEK_SET);
        std::string str;
        str.resize(fsize);
        fread(&str[0], 1, fsize, ff);
        fclose(ff);

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
    fscanf(fsession, "%lu", &startTime);
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
    fwrite(bigJson.data(), 1, bigJson.size(), fout);
    fclose(fout);

    double tDuration = stm_sec(stm_since(tStart));
    printf("%s  done in %.1fs. Run 'ClangBuildAnalyzer --analyze %s' to analyze it.%s\n", col::kYellow, tDuration, outFile.c_str(), col::kReset);

    return 0;
}


static int RunAnalyze(int argc, const char* argv[])
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
    FILE* ff = fopen(inFile.c_str(), "rb");
    if (!ff)
    {
        printf("%sERROR: failed to open file '%s'.%s\n", col::kRed, inFile.c_str(), col::kReset);
        return 1;
    }
    fseek(ff, 0, SEEK_END);
    size_t fsize = ftell(ff);
    fseek(ff, 0, SEEK_SET);
    std::string inFileStr;
    inFileStr.resize(fsize);
    fread(&inFileStr[0], 1, fsize, ff);
    fclose(ff);
    
    BuildEvents events = ParseBuildEvents(inFileStr);
    if (events.empty())
    {
        printf("%s  no trace events found.%s\n", col::kYellow, col::kReset);
        return 1;
    }
    
    DoAnalysis(events);
    
    double tDuration = stm_sec(stm_since(tStart));
    printf("%s  done in %.1fs.%s\n", col::kYellow, tDuration, col::kReset);
    
    return 0;
}


static int ProcessCommands(int argc, const char* argv[])
{
    if (strcmp(argv[1], "--start") == 0)
        return RunStart(argc, argv);
    if (strcmp(argv[1], "--stop") == 0)
        return RunStop(argc, argv);
    if (strcmp(argv[1], "--analyze") == 0)
        return RunAnalyze(argc, argv);
    
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
