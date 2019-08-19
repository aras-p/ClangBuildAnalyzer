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

BuildEvents ParseBuildEvents(const std::string& jsonText)
{
    BuildEvents res;
    
    json js = json::parse(jsonText); //@TODO: exceptions
    const auto& jsFiles = js.find("files");
    if (jsFiles == js.end() || !jsFiles->is_object())
    {
        printf("%sERROR: JSON root object should contain 'files' object.%s\n", col::kRed, col::kReset);
        res.clear(); return res;
    }
    
    res.reserve(4096);
    
    for (const auto& jsFilesEl : jsFiles->items())
    {
        const std::string& curFileName = jsFilesEl.key();
        const auto& jsFileEvents = jsFilesEl.value().find("traceEvents");
        if (jsFileEvents == jsFilesEl.value().end() || !jsFileEvents->is_array())
        {
            printf("%sERROR: JSON file element should contain 'traceEvents', not found in '%s'.%s\n", col::kRed, curFileName.c_str(), col::kReset);
            res.clear(); return res;
        }
        
        BuildEvents fileEvents;
        fileEvents.reserve(jsFileEvents->size());
        std::string curOptModule;
        for (const auto& jsEvent : *jsFileEvents)
        {
            if (!jsEvent.is_object())
            {
                printf("%sERROR: JSON file 'traceEvents' should contain objects, in '%s'.%s\n", col::kRed, curFileName.c_str(), col::kReset);
                res.clear(); return res;
            }
            const auto& jsPid = jsEvent.find("pid");
            const auto jsEnd = jsEvent.end();
            if (jsPid != jsEnd && *jsPid != 1)
                continue;
            const auto& jsTid = jsEvent.find("tid");
            if (jsTid != jsEnd && *jsTid != 0)
                continue;
            const auto& jsPh = jsEvent.find("ph");
            const auto& jsTs = jsEvent.find("ts");
            const auto& jsDur = jsEvent.find("dur");
            const auto& jsName = jsEvent.find("name");
            if (jsPh == jsEnd || jsTs == jsEnd || jsDur == jsEnd || jsName == jsEnd)
                continue;
            if (*jsPh != "X")
                continue;
            const std::string& name = *jsName;
            
            BuildEvent event;
            if (name == "ExecuteCompiler")
                event.type = BuildEventType::kCompiler;
            else if (name == "Frontend")
                event.type = BuildEventType::kFrontend;
            else if (name == "Backend")
                event.type = BuildEventType::kBackend;
            else if (name == "Source")
                event.type = BuildEventType::kParseFile;
            else if (name == "ParseTemplate")
                event.type = BuildEventType::kParseTemplate;
            else if (name == "ParseClass")
                event.type = BuildEventType::kParseClass;
            else if (name == "InstantiateClass")
                event.type = BuildEventType::kInstantiateClass;
            else if (name == "InstantiateFunction")
                event.type = BuildEventType::kInstantiateFunction;
            else if (name == "PerformPendingInstantiations")
                continue;
            else if (name == "CodeGen Function")
                continue;
            else if (name == "OptModule")
                event.type = BuildEventType::kOptModule;
            else if (name == "OptFunction")
                event.type = BuildEventType::kOptFunction;
            else if (name == "RunPass")
                continue;
            else
            {
                printf("%sWARN: unknown trace event '%s' in '%s', skipping.%s\n", col::kYellow, name.c_str(), curFileName.c_str(), col::kReset);
                continue;
            }

            event.ts = *jsTs;
            event.dur = *jsDur;
            
            const auto& jsArgs = jsEvent.find("args");
            if (jsArgs == jsEnd || !jsArgs->is_object())
            {
                printf("%sWARN: %s trace event in '%s' does not have 'args', skipping.%s\n", col::kYellow, name.c_str(), curFileName.c_str(), col::kReset);
                continue;
            }
            const auto& jsDetail = jsArgs->find("detail");
            if (jsDetail == jsArgs->end())
            {
                printf("%sWARN: %s trace event 'args' in '%s' does not have 'detail', skipping.%s\n", col::kYellow, name.c_str(), curFileName.c_str(), col::kReset);
                continue;
            }
            event.detail = *jsDetail;
            if (event.detail.empty() && event.type == BuildEventType::kCompiler)
                event.detail = curFileName;
            fileEvents.push_back(event);
        }
        
        FindParentChildrenIndices(fileEvents);
        if (!fileEvents.empty())
        {
            if (fileEvents.back().parent != -1)
            {
                printf("%sERROR: the last trace event should be root; was not in '%s'.%s\n", col::kRed, curFileName.c_str(), col::kReset);
                res.clear(); return res;
            }
        }
        AddEvents(res, fileEvents);
    }
    
    //DebugPrintEvents(res);
    return res;
}
