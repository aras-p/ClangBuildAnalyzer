// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "BuildEvents.h"
#include "Colors.h"
#include "external/json.hpp"

using json = nlohmann::json;

static void DebugPrintEvents(const BuildEvents& events)
{
    for (size_t i = 0; i < events.size(); ++i)
    {
        const BuildEvent& event = events[i];
        printf("%4zi: t=%i t1=%7llu t2=%7llu par=%4i ch=%4zi det=%s\n", i, event.type, event.ts, event.ts+event.dur, event.parent, event.children.size(), event.detail.substr(0,130).c_str());
    }
}

static void FindParentChildrenIndices(BuildEvents& events)
{
    for (int i = 0, n = (int)events.size(); i != n; ++i)
    {
        BuildEvent& ev = events[i];
        for (int j = i + 1; j != n; ++j)
        {
            BuildEvent& ev2 = events[j];
            if (ev.ts <= ev2.ts+ev2.dur && ev.ts+ev.dur >= ev2.ts)
            {
                ev.parent = j;
                ev2.children.push_back(i);
                break;
            }
        }
    }
}

static void AddEvents(BuildEvents& res, const BuildEvents& add)
{
    int offset = (int)res.size();
    res.insert(res.end(), add.begin(), add.end());
    for (size_t i = offset, n = res.size(); i != n; ++i)
    {
        BuildEvent& ev = res[i];
        if (ev.parent >= 0)
            ev.parent += offset;
        for (auto& ch : ev.children)
            ch += offset;
    }
}

struct SaxConsumer : public json::json_sax_t
{
    enum State
    {
        kRoot,
        kFiles,
        kFile,
        kEvent,
        kArgs
    };
    State state = kRoot;
    std::string curFileName;
    BuildEvents resultEvents;
    BuildEvents fileEvents;

    int curPid = 1, curTid = 0;
    std::string curPh;
    uint64_t curTs = 0, curDur = 0;
    std::string curName;
    std::string curDetail;
    std::string curKey;
    
    bool start_object(std::size_t elements) override
    {
        if (state == kEvent)
        {
            curPid = 1;
            curTid = 0;
            curPh.clear();
            curTs = curDur = 0;
            curName.clear();
            curDetail.clear();
            curKey.clear();
        }
        return true;
    }
    
    bool key(string_t& val) override
    {
        switch (state)
        {
            case kRoot:
                if (val == "files")
                    state = kFiles;
                break;
            case kFiles:
                curFileName = val;
                fileEvents.clear();
                state = kFile;
                break;
            case kEvent:
                if (val == "args")
                    state = kArgs;
                curKey = val;
                break;
            case kArgs:
                curKey = val;
                break;
            default:
                break;
        }
        return true;
    }

    bool end_object() override
    {
        if (state == kFiles)
            state = kRoot;
        else if (state == kFile)
        {
            state = kFiles;
            FindParentChildrenIndices(fileEvents);
            if (!fileEvents.empty())
            {
                if (fileEvents.back().parent != -1)
                {
                    printf("%sERROR: the last trace event should be root; was not in '%s'.%s\n", col::kRed, curFileName.c_str(), col::kReset);
                    resultEvents.clear();
                    return false;
                }
            }
            AddEvents(resultEvents, fileEvents);
        }
        else if (state == kEvent)
        {
            if (curPid == 1 && curTid == 0 && curPh == "X" && !curName.empty())
            {
                BuildEvent event;
                if (curName == "ExecuteCompiler")
                    event.type = BuildEventType::kCompiler;
                else if (curName == "Frontend")
                    event.type = BuildEventType::kFrontend;
                else if (curName == "Backend")
                    event.type = BuildEventType::kBackend;
                else if (curName == "Source")
                    event.type = BuildEventType::kParseFile;
                else if (curName == "ParseTemplate")
                    event.type = BuildEventType::kParseTemplate;
                else if (curName == "ParseClass")
                    event.type = BuildEventType::kParseClass;
                else if (curName == "InstantiateClass")
                    event.type = BuildEventType::kInstantiateClass;
                else if (curName == "InstantiateFunction")
                    event.type = BuildEventType::kInstantiateFunction;
                else if (curName == "PerformPendingInstantiations")
                    ;
                else if (curName == "CodeGen Function")
                    ;
                else if (curName == "OptModule")
                    event.type = BuildEventType::kOptModule;
                else if (curName == "OptFunction")
                    event.type = BuildEventType::kOptFunction;
                else if (curName == "RunPass")
                    ;
                else if (curName == "RunLoopPass")
                    ;
                else
                {
                    printf("%sWARN: unknown trace event '%s' in '%s', skipping.%s\n", col::kYellow, curName.c_str(), curFileName.c_str(), col::kReset);
                }
                if (event.type != BuildEventType::kUnknown)
                {
                    event.ts = curTs;
                    event.dur = curDur;
                    event.detail = curDetail;
                    if (event.detail.empty() && event.type == BuildEventType::kCompiler)
                        event.detail = curFileName;
                    fileEvents.push_back(event);
                }
            }
        }
        else if (state == kArgs)
            state = kEvent;
        return true;
    }
    
    bool start_array(std::size_t elements) override
    {
        if (state == kFile)
            state = kEvent;
        return true;
    }
    
    bool end_array() override
    {
        if (state == kEvent)
            state = kFile;
        return true;
    }

    bool null() override
    {
        return true;
    }
    
    bool boolean(bool val) override
    {
        return true;
    }
    
    bool number_integer(number_integer_t val) override
    {
        return true;
    }
    
    bool number_unsigned(number_unsigned_t val) override
    {
        if (state == kEvent)
        {
            if (curKey == "pid")
                curPid = (int)val;
            if (curKey == "tid")
                curTid = (int)val;
            if (curKey == "ts")
                curTs = val;
            if (curKey == "dur")
                curDur = val;
        }
        return true;
    }
    
    bool number_float(number_float_t val, const string_t& s) override
    {
        return true;
    }
    
    bool string(string_t& val) override
    {
        if (state == kEvent)
        {
            if (curKey == "ph")
                curPh = val;
            if (curKey == "name")
                curName = val;
        }
        if (state == kArgs)
        {
            if (curKey == "detail")
                curDetail = val;
        }
        return true;
    }
    
    bool parse_error(std::size_t position, const std::string& last_token, const json::exception& ex) override
    {
        printf("%sERROR: JSON parse error %s.%s\n", col::kRed, ex.what(), col::kReset);
        return false;
    }
};

BuildEvents ParseBuildEvents(const std::string& jsonText)
{
    SaxConsumer sax;
    json::sax_parse(jsonText, &sax);
    //DebugPrintEvents(sax.resultEvents);
    return sax.resultEvents;
}
