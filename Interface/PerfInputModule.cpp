#include "General.h"
#include "PerfInputModule.h"
#include "Log.h"

void(*LogFunc)(int, const char*, ...) = nullptr;

extern "C"
{
    DLL_EXPORT_API InputModule* CreateInputModule()
    {
        return new PerfInputModule;
    }

    DLL_EXPORT_API void RegisterLogger(void(*log)(int, const char*, ...))
    {
        LogFunc = log;
    }
}

PerfInputModule::PerfInputModule()
{
    //
}

PerfInputModule::~PerfInputModule()
{
    //
}

const char* PerfInputModule::ReportName()
{
    return "perf input module";
}

const char* PerfInputModule::ReportVersion()
{
    return "0.1-dev";
}

void PerfInputModule::ReportFeatures(IMF_SET &set)
{
    // nullify set
    IMF_CREATE(set);

    // add features we support
    //IMF_ADD(set, IMF_FLAT_PROFILE);
    //IMF_ADD(set, IMF_CALL_GRAPH);
}

bool PerfInputModule::LoadFile(const char* file, const char* binaryFile)
{
    // TODO

    return true;
}

void PerfInputModule::GetClassTable(std::vector<ClassEntry> &dst)
{
    dst.clear();

    // TODO
}

void PerfInputModule::GetFunctionTable(std::vector<FunctionEntry> &dst)
{
    dst.clear();

    // TODO
}

void PerfInputModule::GetFlatProfileData(std::vector<FlatProfileRecord> &dst)
{
    dst.clear();

    // TODO
}

void PerfInputModule::GetCallGraphMap(CallGraphMap &dst)
{
    dst.clear();

    // TODO
}
