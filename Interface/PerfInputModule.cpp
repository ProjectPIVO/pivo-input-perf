#include "General.h"
#include "PerfFile.h"
#include "UnitIdentifiers.h"
#include "FlatProfileStructs.h"
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

    // flat profile is supported
    IMF_ADD(set, IMF_FLAT_PROFILE);
}

bool PerfInputModule::LoadFile(const char* file, const char* binaryFile)
{
    m_pfile = PerfFile::Load(file, binaryFile);

    return (m_pfile != nullptr);
}

void PerfInputModule::GetClassTable(std::vector<ClassEntry> &dst)
{
    dst.clear();

    // TODO
}

void PerfInputModule::GetFunctionTable(std::vector<FunctionEntry> &dst)
{
    dst.clear();

    m_pfile->FillFunctionTable(dst);
}

void PerfInputModule::GetFlatProfileData(std::vector<FlatProfileRecord> &dst)
{
    dst.clear();

    m_pfile->FillFlatProfileTable(dst);
}

void PerfInputModule::GetCallGraphMap(CallGraphMap &dst)
{
    dst.clear();

    // TODO
}
