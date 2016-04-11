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
