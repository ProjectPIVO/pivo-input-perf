#ifndef PIVO_PERF_MODULE_H
#define PIVO_PERF_MODULE_H

#include "InputModule.h"
#include "InputModuleFeatures.h"

class PerfFile;

extern void(*LogFunc)(int, const char*, ...);

class PerfInputModule : public InputModule
{
    public:
        PerfInputModule();
        ~PerfInputModule();

        virtual const char* ReportName();
        virtual const char* ReportVersion();
        virtual void ReportFeatures(IMF_SET &set);
        virtual bool LoadFile(const char* file, const char* binaryFile);
        virtual void GetClassTable(std::vector<ClassEntry> &dst);
        virtual void GetFunctionTable(std::vector<FunctionEntry> &dst);
        virtual void GetFlatProfileData(std::vector<FlatProfileRecord> &dst);
        virtual void GetCallGraphMap(CallGraphMap &dst);
        virtual void GetCallTreeMap(CallTreeMap &dst);

    protected:
        //

    private:
        PerfFile* m_pfile;
};

#endif
