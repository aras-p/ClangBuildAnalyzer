// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "BuildEvents.h"
#include "Colors.h"
#include "external/sajson.h"

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

struct JsonTraverser
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
    
    void traverse(const sajson::value& node)
    {
        switch(node.get_type())
        {
            case sajson::TYPE_OBJECT:
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
                
                for (size_t i = 0, n = node.get_length(); i != n; ++i)
                {
                    const auto& key = node.get_object_key(i).as_string();
                    switch (state)
                    {
                        case kRoot:
                            if (key == "files")
                                state = kFiles;
                            break;
                        case kFiles:
                            curFileName = key;
                            fileEvents.clear();
                            state = kFile;
                            break;
                        case kEvent:
                            if (key == "args")
                                state = kArgs;
                            curKey = key;
                            break;
                        case kArgs:
                            curKey = key;
                            break;
                        default:
                            break;
                    }
                    
                    traverse(node.get_object_value(i));
                }
                
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
                            return;
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
                break;
            case sajson::TYPE_ARRAY:
                {
                    if (state == kFile)
                        state = kEvent;
                    auto length = node.get_length();
                    for (size_t i = 0; i < length; ++i)
                        traverse(node.get_array_element(i));
                    if (state == kEvent)
                        state = kFile;
                }
                break;
            case sajson::TYPE_DOUBLE:
            case sajson::TYPE_INTEGER:
                if (state == kEvent)
                {
                    if (curKey == "pid")
                        curPid = (int)node.get_number_value();
                    if (curKey == "tid")
                        curTid = (int)node.get_number_value();
                    if (curKey == "ts")
                        curTs = node.get_number_value();
                    if (curKey == "dur")
                        curDur = node.get_number_value();
                }
                break;
            case sajson::TYPE_STRING:
                if (state == kEvent)
                {
                    if (curKey == "ph")
                        curPh = node.as_string();
                    if (curKey == "name")
                        curName = node.as_string();
                }
                if (state == kArgs)
                {
                    if (curKey == "detail")
                        curDetail = node.as_string();
                }
                break;
            default:
                break;
        }
    }
};

BuildEvents ParseBuildEvents(std::string& jsonText)
{
    const sajson::document& doc = sajson::parse(sajson::dynamic_allocation(), sajson::mutable_string_view(jsonText.size(), &jsonText[0]));
    if (!doc.is_valid())
    {
        printf("%sERROR: JSON parse error %s.%s\n", col::kRed, doc.get_error_message_as_cstring(), col::kReset);
        return BuildEvents();
    }
    
    JsonTraverser traverser;
    traverser.traverse(doc.get_root());
    //DebugPrintEvents(sax.resultEvents);
    return traverser.resultEvents;
}
