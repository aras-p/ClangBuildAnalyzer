// Clang Build Analyzer https://github.com/aras-p/ClangBuildAnalyzer
// SPDX-License-Identifier: Unlicense
#include "BuildEvents.h"
#include "Colors.h"
#include "external/sajson.h"
#include <assert.h>
#include <unordered_map>
#include <iterator>

static void DebugPrintEvents(const BuildEvents& events, const BuildNames& names)
{
    for (size_t i = 0; i < events.size(); ++i)
    {
        const BuildEvent& event = events[i];
        printf("%4zi: t=%i t1=%7llu t2=%7llu par=%4i ch=%4zi det=%s\n", i, event.type, event.ts, event.ts+event.dur, event.parent, event.children.size(), names[event.detailIndex].substr(0,130).c_str());
    }
}

static void FindParentChildrenIndices(BuildEvents& events)
{
    if (events.empty())
        return;

    // sort events by start time so that parent events go before child events
    std::vector<int> sortedIndices;
    sortedIndices.resize(events.size());
    for (int i = 0, n = (int)events.size(); i != n; ++i)
        sortedIndices[i] = i;
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int ia, int ib){
        const auto& ea = events[ia];
        const auto& eb = events[ib];
        if (ea.ts != eb.ts)
            return ea.ts < eb.ts;
        // break start time ties by making longer events go first (they must be parent)
        if (ea.dur != eb.dur)
            return ea.dur > eb.dur;
        // break ties by assuming that later events in sequence must start parent
        return ia > ib;
    });

    // figure out the event hierarchy; for now the parent/child indices are into
    // the "sortedIndices" array and not event indices in the "events" array
    int root = 0;
    BuildEvent* evRoot = &events[sortedIndices[root]];
    evRoot->parent = -1;
    for (int i = 1, n = (int)events.size(); i != n; ++i)
    {
        BuildEvent* ev2 = &events[sortedIndices[i]];
        while (root != -1)
        {
            // add slice if within bounds
            if (ev2->ts >= evRoot->ts && ev2->ts+ev2->dur <= evRoot->ts+evRoot->dur)
            {
                ev2->parent = root;
                evRoot->children.push_back(i);
                break;
            }
            
            root = evRoot->parent;
            if (root != -1)
                evRoot = &events[sortedIndices[root]];
        }
        if (root == -1)
        {
            ev2->parent = -1;
        }
        root = i;
        evRoot = &events[sortedIndices[i]];
    }

    // fixup event parent/child indices to be into "events" array
    for (auto& e : events)
    {
        for (auto& c : e.children)
            c = sortedIndices[c];
        if (e.parent != -1)
            e.parent = sortedIndices[e.parent];
    }
}

static void AddEvents(BuildEvents& res, BuildEvents& add)
{
    int offset = (int)res.size();
    std::move(add.begin(), add.end(), std::back_inserter(res));
    add.clear();
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
    JsonTraverser(BuildEvents& outEvents, BuildNames& outNames)
    : resultEvents(outEvents), resultNames(outNames)
    {
        NameToIndex(""); // make sure zero index is empty
    }

    std::string curFileName;
    BuildEvents& resultEvents;
    BuildNames& resultNames;
    BuildEvents fileEvents;

    std::unordered_map<std::string, int> nameToIndex;

    int NameToIndex(const std::string& name)
    {
        auto it = nameToIndex.find(name);
        if (it != nameToIndex.end())
            return it->second;
        int index = (int)nameToIndex.size();
        nameToIndex.insert(std::make_pair(name, index));
        resultNames.push_back(name);
        return index;
    }

    void ParseRoot(const sajson::value& node)
    {
        if (node.get_type() != sajson::TYPE_OBJECT)
        {
            printf("%sERROR: root of JSON should be an object.%s\n", col::kRed, col::kReset);
            resultEvents.clear();
            return;
        }
        const auto& files = node.get_value_of_key(sajson::literal("files"));
        ParseFiles(files);
    }

    void ParseFiles(const sajson::value& node)
    {
        if (node.get_type() != sajson::TYPE_OBJECT)
        {
            printf("%sERROR: 'files' of JSON should be an object.%s\n", col::kRed, col::kReset);
            resultEvents.clear();
            return;
        }

        for (size_t i = 0, n = node.get_length(); i != n; ++i)
        {
            const auto& fileName = node.get_object_key(i);
            curFileName = fileName.as_string();
            const auto& fileVal = node.get_object_value(i);
            if (fileVal.get_type() != sajson::TYPE_OBJECT)
            {
                printf("%sERROR: 'files' elements in JSON should be objects.%s\n", col::kRed, col::kReset);
                resultEvents.clear();
                return;
            }
            const auto& traceEventsVal = fileVal.get_value_of_key(sajson::literal("traceEvents"));
            ParseTraceEvents(traceEventsVal);
        }
    }

    void ParseTraceEvents(const sajson::value& node)
    {
        if (node.get_type() != sajson::TYPE_ARRAY)
        {
            printf("%sERROR: 'traceEvents' of JSON should be an array.%s\n", col::kRed, col::kReset);
            resultEvents.clear();
            return;
        }
        fileEvents.clear();
        fileEvents.reserve(node.get_length());
        for (size_t i = 0, n = node.get_length(); i != n; ++i)
        {
            ParseEvent(node.get_array_element(i));
        }

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

    static bool StrEqual(const sajson::string& s1, const sajson::string& s2)
    {
        if (s1.length() != s2.length())
            return false;
        if (memcmp(s1.data(), s2.data(), s2.length()) != 0)
            return false;
        return true;
    }

    const sajson::string kPid = sajson::literal("pid");
    const sajson::string kTid = sajson::literal("tid");
    const sajson::string kPh = sajson::literal("ph");
    const sajson::string kName = sajson::literal("name");
    const sajson::string kTs = sajson::literal("ts");
    const sajson::string kDur = sajson::literal("dur");
    const sajson::string kArgs = sajson::literal("args");
    const sajson::string kDetail = sajson::literal("detail");

    void ParseEvent(const sajson::value& node)
    {
        if (node.get_type() != sajson::TYPE_OBJECT)
        {
            printf("%sERROR: 'traceEvents' elements in JSON should be objects.%s\n", col::kRed, col::kReset);
            resultEvents.clear();
            return;
        }

        BuildEvent event;
        for (size_t i = 0, n = node.get_length(); i != n; ++i)
        {
            const auto& nodeKey = node.get_object_key(i);
            const auto& nodeVal = node.get_object_value(i);
            if (StrEqual(nodeKey, kPid))
            {
                if (nodeVal.get_type() == sajson::TYPE_INTEGER && nodeVal.get_integer_value() != 1)
                    return;
            }
            else if (StrEqual(nodeKey, kTid))
            {
                if (nodeVal.get_type() == sajson::TYPE_INTEGER && nodeVal.get_integer_value() != 0)
                    return;
            }
            else if (StrEqual(nodeKey, kPh))
            {
                if (nodeVal.get_type() != sajson::TYPE_STRING || strcmp(nodeVal.as_cstring(), "X") != 0)
                    return;
            }
            else if (StrEqual(nodeKey, kName))
            {
                if (nodeVal.get_type() != sajson::TYPE_STRING)
                    return;
                const char* name = nodeVal.as_cstring();
                if (!strcmp(name, "ExecuteCompiler"))
                    event.type = BuildEventType::kCompiler;
                else if (!strcmp(name, "Frontend"))
                    event.type = BuildEventType::kFrontend;
                else if (!strcmp(name, "Backend"))
                    event.type = BuildEventType::kBackend;
                else if (!strcmp(name, "Source"))
                    event.type = BuildEventType::kParseFile;
                else if (!strcmp(name, "ParseTemplate"))
                    event.type = BuildEventType::kParseTemplate;
                else if (!strcmp(name, "ParseClass"))
                    event.type = BuildEventType::kParseClass;
                else if (!strcmp(name, "InstantiateClass"))
                    event.type = BuildEventType::kInstantiateClass;
                else if (!strcmp(name, "InstantiateFunction"))
                    event.type = BuildEventType::kInstantiateFunction;
                else if (!strcmp(name, "PerformPendingInstantiations"))
                    ;
                else if (!strcmp(name, "CodeGen Function"))
                    ;
                else if (!strcmp(name, "OptModule"))
                    event.type = BuildEventType::kOptModule;
                else if (!strcmp(name, "OptFunction"))
                    event.type = BuildEventType::kOptFunction;
                else if (!strcmp(name, "RunPass"))
                    ;
                else if (!strcmp(name, "RunLoopPass"))
                    ;
                else
                {
                    printf("%sWARN: unknown trace event '%s' in '%s', skipping.%s\n", col::kYellow, name, curFileName.c_str(), col::kReset);
                }
                if (event.type== BuildEventType::kUnknown)
                    return;
            }
            else if (StrEqual(nodeKey, kTs))
            {
                if (nodeVal.get_type() != sajson::TYPE_INTEGER)
                    return;
                event.ts = nodeVal.get_integer_value();
            }
            else if (StrEqual(nodeKey, kDur))
            {
                if (nodeVal.get_type() != sajson::TYPE_INTEGER)
                    return;
                event.dur = nodeVal.get_integer_value();
            }
            else if (StrEqual(nodeKey, kArgs))
            {
                if (nodeVal.get_type() == sajson::TYPE_OBJECT && nodeVal.get_length() == 1)
                {
                    const auto& nodeDetail = nodeVal.get_object_value(0);
                    if (nodeDetail.get_type() == sajson::TYPE_STRING)
                        event.detailIndex = NameToIndex(nodeDetail.as_string());
                }
            }
        }

        if (event.detailIndex == 0 && event.type == BuildEventType::kCompiler)
            event.detailIndex = NameToIndex(curFileName);
        fileEvents.emplace_back(event);
    }
};

void ParseBuildEvents(std::string& jsonText, BuildEvents& outEvents, BuildNames& outNames)
{
    const sajson::document& doc = sajson::parse(sajson::dynamic_allocation(), sajson::mutable_string_view(jsonText.size(), &jsonText[0]));
    if (!doc.is_valid())
    {
        printf("%sERROR: JSON parse error %s.%s\n", col::kRed, doc.get_error_message_as_cstring(), col::kReset);
        return;
    }

    JsonTraverser traverser(outEvents, outNames);
    traverser.ParseRoot(doc.get_root());
    //DebugPrintEvents(outEvents, outNames);
}
