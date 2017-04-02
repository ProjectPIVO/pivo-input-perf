/**
 * Copyright (C) 2016 Martin Ubl <http://pivo.kennny.cz>
 *
 * This file is part of PIVO perf input module.
 *
 * PIVO perf input module is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * PIVO perf input module is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PIVO perf input module. If not,
 * see <http://www.gnu.org/licenses/>.
 **/

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

    // call graph is supported
    IMF_ADD(set, IMF_CALL_GRAPH);

    // we calculate inclusive time using our own mechanisms
    IMF_ADD(set, IMF_INCLUSIVE_TIME);

    // call tree is supported
    IMF_ADD(set, IMF_CALL_TREE);

    // heat map is supported
    IMF_ADD(set, IMF_HEAT_MAP_DATA);
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

    m_pfile->FillCallGraphMap(dst);
}

void PerfInputModule::GetCallTreeMap(CallTreeMap &dst)
{
    dst.clear();

    m_pfile->FillCallTreeMap(dst);
}

void PerfInputModule::GetHeatMapData(TimeHistogramVector &dst)
{
    dst.clear();

    m_pfile->FillHeatMapData(dst);
}
