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
#include "InputModule.h"
#include "PerfInputModule.h"
#include "PerfFile.h"
#include "PerfRecords.h"
#include "Log.h"
#include "Helpers.h"
#include "../config_perf.h"

#include <set>
#include <algorithm>
#include <stack>
#include <sstream>
#include <iomanip>

#include <sys/stat.h>

PerfFile::PerfFile()
{
    //
}

PerfFile* PerfFile::Load(const char* filename, const char* binaryfilename)
{
    LogFunc(LOG_DEBUG, "Loading perf record file %s", filename);

    FILE* pf = fopen(filename, "rb");
    if (!pf)
    {
        LogFunc(LOG_ERROR, "Couldn't find perf record file %s", filename);
        return nullptr;
    }

    PerfFile* pfile = new PerfFile();
    pfile->m_file = pf;

    // perform all reading
    if (!pfile->ReadAndCheckHeader() ||
        !pfile->ReadAttributes() ||
        !pfile->ReadTypes() ||
        !pfile->ReadData())
    {
        fclose(pf);
        delete pfile;
        return nullptr;
    }

    // sort records by time
    std::sort(pfile->m_records.begin(), pfile->m_records.end(), PerfRecordTimeSortPredicate());

    pfile->ResolveSymbols(binaryfilename);
    pfile->ProcessMemoryMapping();
    pfile->FilterUsedSymbols();

    pfile->ProcessFlatProfile();
    pfile->ProcessCallGraph();
    pfile->ProcessCallTree();

    fclose(pf);

    return pfile;
}

void PerfFile::ProcessFlatProfile()
{
    m_flatProfile.resize(m_functionTable.size());

    FlatProfileRecord *fp;

    LogFunc(LOG_INFO, "Processing flat profile data...");

    // prepare flat profile table, it will match function table at first stage of filling
    for (int i = 0; i < m_functionTable.size(); i++)
    {
        fp = &m_flatProfile[i];

        fp->functionId = i;
        fp->timeTotal = 0;
        fp->timeTotalPct = 0.0f;
    }

    record_sample* sample;
    FunctionEntry* fet;

    uint32_t findex, childindex;
    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            sample = ((record_sample*)itr);

            fet = GetFunctionByAddress(sample->ip, &findex);
            // for now, exclude kernel symbols (for sanity reasons)
            if (fet && fet->functionType != FET_KERNEL)
            {
                m_flatProfile[findex].timeTotal += 1.0;

                // now calculate inclusive time

                // 2 is the right value, since IP callchain contains invalid address ("stopper") on top
                // and self as second record
                for (uint64_t i = 2; i < sample->callchain->nr; i++)
                {
                    fet = GetFunctionByAddress(sample->callchain->ips[i], &childindex);
                    // also exclude kernel calls for now
                    if (fet && fet->functionType != FET_KERNEL)
                        m_flatProfile[childindex].timeTotalInclusive += 1.0;
                }
            }
        }
    }

    LogFunc(LOG_VERBOSE, "Finalizing inclusive time calculation...");

    double maxInclusiveTime = 0.01;
    for (size_t i = 0; i < m_flatProfile.size(); i++)
    {
        if (m_flatProfile[i].timeTotalInclusive > maxInclusiveTime)
            maxInclusiveTime = m_flatProfile[i].timeTotalInclusive;
    }

    for (size_t i = 0; i < m_flatProfile.size(); i++)
    {
        m_flatProfile[i].timeTotalInclusivePct = m_flatProfile[i].timeTotalInclusive / maxInclusiveTime;
    }
}

void PerfFile::ProcessCallGraph()
{
    FlatProfileRecord *fp;
    record_sample* sample;
    FunctionEntry* fet;

    LogFunc(LOG_INFO, "Processing call graph...");

    // nullify call counts
    for (int i = 0; i < m_functionTable.size(); i++)
    {
        fp = &m_flatProfile[i];
        fp->callCount = 0;
    }

    uint64_t tmpip;
    uint32_t findex, srcIndex, dstIndex;
    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            sample = ((record_sample*)itr);

            fet = GetFunctionByAddress(sample->ip, &findex);
            // for now, exclude kernel symbols from output (for sanity reasons)
            if (fet /*&& fet->functionType != FET_KERNEL*/)
            {
                dstIndex = findex;

                // 2 is the right value, since IP callchain contains invalid address ("stopper") on top
                // and self as second record
                for (uint64_t i = 2; i < sample->callchain->nr; i++)
                {
                    tmpip = sample->callchain->ips[i];

                    fet = GetFunctionByAddress(tmpip, &srcIndex);
                    // also exclude kernel calls for now
                    if (fet /*&& fet->functionType != FET_KERNEL*/)
                    {
                        if (m_callGraph.find(srcIndex) == m_callGraph.end() ||
                            m_callGraph[srcIndex].find(dstIndex) == m_callGraph[srcIndex].end())
                        {
                            m_callGraph[srcIndex][dstIndex] = 0;
                        }

                        // rather than call count, we use something like "samples count" here
                        m_callGraph[srcIndex][dstIndex] += 1;
                        m_flatProfile[dstIndex].callCount++;

                        dstIndex = srcIndex;
                    }
                }
            }
        }
    }
}

CallTreeNode* PerfFile::CreateCallTreeNode(uint32_t functionId, CallTreeNode* childOf)
{
    CallTreeNode* node = new CallTreeNode;
    node->functionId = functionId;
    node->timeTotal = 0.0;
    node->timeTotalPct = 0.0;
    node->sampleCount = 0;
    node->parent = childOf;
    return node;
}

CallTreeNode* PerfFile::InsertIntoCallTree(std::vector<uint32_t> &path, uint32_t finalFunctionId)
{
    // on zero-length path we have nothing to do
    if (path.size() == 0)
        return nullptr;

    uint32_t i = path.size();
    CallTreeNode* curNode = nullptr;

    // traverse all points in path
    do
    {
        // a loop in such form will allow us to iterate from end to beginning, not
        // excluding zero and not subtract from zero (which would cause overflow)

        i--;

        // if no node selected yet, that means, we need to select root node
        if (curNode == nullptr)
        {
            // find root node in calltree map of root nodes
            auto itr = m_callTree.find(path[i]);
            // if not found, create
            if (itr == m_callTree.end())
            {
                curNode = CreateCallTreeNode(path[i], nullptr);
                m_callTree[path[i]] = curNode;
            }
            else
                curNode = itr->second;
        }
        else // otherwise search in current node children map
        {
            // find node in children map
            auto itr = curNode->children.find(path[i]);
            // if not found, create
            if (itr == curNode->children.end())
            {
                curNode->children[path[i]] = CreateCallTreeNode(path[i], curNode);
                curNode = curNode->children[path[i]];
            }
            else
                curNode = itr->second;
        }
    }
    while (i != 0);

    return curNode;
}

void PerfFile::AccumulateCallTreeTime(CallTreeNode* node, double addTime, bool addSample)
{
    // roll up to root node and add "inclusive" time
    CallTreeNode* tmp = node;
    while (tmp != nullptr)
    {
        tmp->timeTotal += addTime;
        if (addSample)
            tmp->sampleCount++;
        tmp = tmp->parent;
    }
}

void PerfFile::ProcessCallTree()
{
    record_sample* sample;
    FunctionEntry* fet;
    std::vector<uint32_t> callPath;
    CallTreeNode* ctn;

    LogFunc(LOG_INFO, "Processing call tree...");

    uint32_t findex, childindex;
    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            sample = ((record_sample*)itr);

            fet = GetFunctionByAddress(sample->ip, &findex);
            // for now, exclude kernel symbols from output (for sanity reasons)
            if (fet && fet->functionType != FET_KERNEL)
            {
                callPath.clear();

                callPath.push_back(findex);

                // 2 is the right value, since IP callchain contains invalid address ("stopper") on top
                // and self as second record
                for (uint64_t i = 2; i < sample->callchain->nr; i++)
                {
                    fet = GetFunctionByAddress(sample->callchain->ips[i], &childindex);
                    if (fet)
                        callPath.push_back(childindex);
                }

                // insert path into call tree
                ctn = InsertIntoCallTree(callPath, findex);
                if (ctn)
                    AccumulateCallTreeTime(ctn, 1.0, true);
            }
        }
    }

    // root nodes always have the largest inclusive time portions
    double maxTime = 0.0;
    for (auto itr : m_callTree)
            maxTime += itr.second->timeTotal;

    LogFunc(LOG_VERBOSE, "Thresholding sampled paths...");
    LogFunc(LOG_VERBOSE, "Total samples: %u, threshold: %u", (uint64_t)maxTime, (uint64_t)(maxTime*CALL_TREE_INCLUSIVE_TIME_THRESHOLD));

    if (maxTime > 0.0)
    {
        // traverse using iterative DFS to count time precentages

        CallTreeNode* curr;
        std::stack<CallTreeNode*> dstack;
        for (auto itr : m_callTree)
            dstack.push(itr.second);

        while (!dstack.empty())
        {
            curr = dstack.top();
            dstack.pop();

            curr->timeTotalPct = curr->timeTotal / maxTime;

            // exclude function calls with less than THRESHOLD percentage of inclusive time
            if (curr->timeTotalPct < CALL_TREE_INCLUSIVE_TIME_THRESHOLD)
            {
                if (curr->parent)
                    curr->parent->children.erase(curr->functionId);
                else
                    m_callTree.erase(curr->functionId);
            }

            // it's a tree, we don't need to check traversal state
            for (auto itr : curr->children)
                dstack.push(itr.second);
        }
    }
}

void PerfFile::AddFilenameForMapping(uint64_t address, uint64_t length, const char* filename)
{
    m_mmapFiles.push_back({ address, length, filename });
}

const char* PerfFile::RetrieveFilenameForMapping(uint64_t address)
{
    for (MemoryRegionFile &itr : m_mmapFiles)
    {
        if (address >= itr.base && address < itr.base+itr.length)
            return itr.filename.c_str();
    }

    return nullptr;
}

void PerfFile::ProcessMemoryMapping()
{
    LogFunc(LOG_VERBOSE, "Processing memory mappings...");

    // automatically map all kernel addresses mapped via mmap
    // the other symbols mapped via mmap2 are resolved later
    record_mmap* mm;
    record_mmap2* mm2;
    char* tmp;
    int i;

    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_MMAP)
        {
            mm = (record_mmap*)itr;
            AddMemoryMapping(mm->start, mm->len);
            if (!RetrieveFilenameForMapping(mm->start))
            {
                for (i = strlen(mm->filename)-1; i > 0; i--)
                    if (mm->filename[i] == '/' || mm->filename[i] == '\\')
                        break;
                if (i > 0)
                    i++;
                AddFilenameForMapping(mm->start, mm->len, &mm->filename[i]);
            }
        }
        else if (itr->type == PERF_RECORD_MMAP2)
        {
            mm2 = (record_mmap2*)itr;
            if (!RetrieveFilenameForMapping(mm2->start))
            {
                for (i = strlen(mm2->filename)-1; i > 0; i--)
                    if (mm2->filename[i] == '/' || mm2->filename[i] == '\\')
                        break;
                if (i > 0)
                    i++;
                AddFilenameForMapping(mm2->start, mm2->len, &mm2->filename[i]);
            }
        }
    }
}

void PerfFile::AddMemoryMapping(uint64_t start, uint64_t length)
{
    m_memoryMappings.push_back(MemoryRegion(start, length));
}

bool PerfFile::IsWithinMemoryMapping(uint64_t address)
{
    for (MemoryRegion &itr : m_memoryMappings)
    {
        if (itr.first <= address && itr.first + itr.second > address)
            return true;
    }

    return false;
}

void PerfFile::FilterUsedSymbols()
{
    uint32_t findex;
    uint64_t tmpip;
    FunctionEntry* fet;
    record_sample* sample;
    std::stringstream stream;

    FunctionEntry nonexist;
    nonexist.classId = NO_CLASS;
    nonexist.functionType = FET_MISC;
    nonexist.name = "<__unresolved_symbol__>";

    LogFunc(LOG_INFO, "Filtering symbols...");

    std::set<uint64_t> usedIPs;

    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            sample = ((record_sample*)itr);

            nonexist.name = "<__unresolved_symbol__>";

            // if the symbol is not within mapped region, fake the mapping with unresolved symbol
            if (!IsWithinMemoryMapping(sample->ip))
            {
                LogFunc(LOG_DEBUG, "Could not find symbol 0x%.16llX", sample->ip);
                // add fake mapping to <-100;100> address range around sampled IP
                if (sample->ip >= 100)
                    nonexist.address = sample->ip - 100;
                else
                    nonexist.address = 0;

                const char* memname = RetrieveFilenameForMapping(sample->ip);
                if (memname != nullptr)
                {
                    nonexist.name = std::string(memname);
                    stream << std::setfill('0') << std::setw(16)
                           << std::hex << sample->ip;
                    nonexist.name += std::string("::0x") + stream.str().c_str();
                    stream.str(std::string());
                    stream.clear();
                }
                else
                {
                    stream << std::setfill('0') << std::setw(16)
                           << std::hex << sample->ip;
                    nonexist.name = std::string("0x") + stream.str().c_str();
                    stream.str(std::string());
                    stream.clear();
                }

                AddMemoryMapping(nonexist.address, 200);
                // and insert fake symbol at that position
                auto pos = std::lower_bound(m_symbolTable.begin(), m_symbolTable.end(), nonexist, FunctionEntrySortPredicate());
                m_symbolTable.insert(pos, FunctionEntry(nonexist));
            }

            fet = GetSymbolByAddress(sample->ip, &findex);
            if (fet && usedIPs.find(fet->address) == usedIPs.end())
            {
                m_functionTable.push_back(*fet);
                usedIPs.insert(fet->address);

                for (uint64_t i = 0; i < sample->callchain->nr; i++)
                {
                    tmpip = sample->callchain->ips[i];

                    nonexist.name = "<__unresolved_symbol__>";

                    // the same magic as before - when the symbol is not within mapped region, fake the mapping
                    if (!IsWithinMemoryMapping(tmpip))
                    {
                        LogFunc(LOG_DEBUG, "Could not find symbol 0x%.16llX", tmpip);
                        if (tmpip >= 100)
                            nonexist.address = tmpip - 100;
                        else
                            nonexist.address = 0;

                        const char* memname = RetrieveFilenameForMapping(tmpip);
                        if (memname != nullptr)
                        {
                            nonexist.name = std::string(memname);
                            stream << std::setfill('0') << std::setw(16)
                                   << std::hex << tmpip;
                            nonexist.name += std::string("::0x") + stream.str().c_str();
                            stream.str(std::string());
                            stream.clear();
                        }
                        else
                        {
                            stream << std::setfill('0') << std::setw(16)
                                   << std::hex << tmpip;
                            nonexist.name = std::string("0x") + stream.str().c_str();
                            stream.str(std::string());
                            stream.clear();
                        }

                        AddMemoryMapping(nonexist.address, 200);
                        auto pos = std::lower_bound(m_symbolTable.begin(), m_symbolTable.end(), nonexist, FunctionEntrySortPredicate());
                        m_symbolTable.insert(pos, FunctionEntry(nonexist));
                    }

                    fet = GetSymbolByAddress(tmpip, &findex);
                    if (fet && usedIPs.find(fet->address) == usedIPs.end())
                    {
                        m_functionTable.push_back(*fet);
                        usedIPs.insert(fet->address);
                    }
                }
            }
        }
    }

    std::sort(m_functionTable.begin(), m_functionTable.end(), FunctionEntrySortPredicate());

    LogFunc(LOG_VERBOSE, "Used symbols: %u", m_functionTable.size());
}

void PerfFile::ResolveSymbols(const char* binaryFilename)
{
    int cnt = 0;
    int ncnt;

    struct stat binStat, tmpStat;

    LogFunc(LOG_VERBOSE, "Loading debug symbols from application binary...");

    // build nm binary call parameters
    const char *argv[] = {NM_BINARY_PATH, "-C", binaryFilename, 0};
    // next argv set for resolving dynamic symbols
    const char *argv_dyn[] = {NM_BINARY_PATH, "-C", "-D", binaryFilename, 0};

    binStat.st_ino = 0;
    tmpStat.st_ino = 0;
    stat(binaryFilename, &binStat);

    // retrieve symbols from binary file
    int readfd = ForkProcessForReading(argv);
    if (readfd > 0)
    {
        ncnt = ResolveSymbolsUsingFD(readfd);
        LogFunc(LOG_VERBOSE, "Loaded %i symbols", ncnt);
        cnt += ncnt;
        close(readfd);
    }
    else
        LogFunc(LOG_ERROR, "Could not execute nm binary for symbol resolving, no symbols loaded");

    LogFunc(LOG_VERBOSE, "Loading kernel debug symbols...");

    // retrieve kernel symbols
    FILE* kallsymfile = fopen("/proc/kallsyms", "r");
    if (kallsymfile)
    {
        readfd = fileno(kallsymfile);
        ncnt = ResolveSymbolsUsingFD(readfd, 0, FET_KERNEL);
        LogFunc(LOG_VERBOSE, "Loaded %i symbols", ncnt);
        cnt += ncnt;
        fclose(kallsymfile);
    }
    else
        LogFunc(LOG_ERROR, "Could not load kernel symbols from /proc/kallsyms");

    LogFunc(LOG_VERBOSE, "Loading debug symbols from dynamically linked libraries...");

    bool isOriginalBinary;
    std::string libpath;
    FunctionEntry* fet;
    // go through all mmap'd records (via mmap2) and try to load debug libraries connected with them
    for (record_mmap2* itr : m_mmaps2)
    {
        // if it's already mapped, let it go - may be duplicate
        if (IsWithinMemoryMapping(itr->start))
            continue;

        // sort to have relevant info every turn
        std::stable_sort(m_symbolTable.begin(), m_symbolTable.end(), FunctionEntrySortPredicate());

        const uint64_t memstart = itr->start - itr->pgoff;

        // if there are some symbols inside this memory region, it's possible that it has been
        // mapped previously, just find some symbol and if it falls into mapped region,
        // mark this region as already mapped
        fet = GetSymbolByAddress(memstart + itr->len - 1, nullptr);
        if (fet && fet->address >= itr->start && fet->address < itr->start+itr->len)
        {
            AddMemoryMapping(itr->start, itr->len);
            // do not resolve any further, collisions possible
            continue;
        }

        // TODO: find more portable way to look for debug library path
        // by default, Debian-based systems stores such libs in /usr/lib/debug
        libpath = "/usr/lib/debug";
        libpath += itr->filename;

        bool useDynamic = false;
        FILE* tst = fopen(libpath.c_str(), "r");
        if (!tst)
        {
            tst = fopen(itr->filename, "r");
            if (!tst)
                continue;

            useDynamic = true;
            libpath = itr->filename;
        }

        fclose(tst);

        // try to stat the file - if the i-nodes are equal with original binary, act like we're loading symbols from original binary
        // this is important when considering directly mmapped binaries, which are not aligned in compile-time
        stat(libpath.c_str(), &tmpStat);

        isOriginalBinary = (binStat.st_ino == tmpStat.st_ino);
        if (isOriginalBinary)
            useDynamic = false;

        LogFunc(LOG_VERBOSE, "Loading debug symbols from %s...", libpath.c_str());

        // substitute binary parameter in "nm" command line

        if (useDynamic)
        {
            argv_dyn[3] = libpath.c_str();
            readfd = ForkProcessForReading(argv_dyn);
        }
        else
        {
            argv[2] = libpath.c_str();
            readfd = ForkProcessForReading(argv);
        }

        if (readfd > 0)
        {
            // the base address is mandatory here, since the memory is mmap'd to
            // another offset in virtual address space, thus all symbols are
            // moved by this offset
            ncnt = ResolveSymbolsUsingFD(readfd, memstart, isOriginalBinary ? FET_DONTCARE : FET_MISC);
            LogFunc(LOG_VERBOSE, "Loaded %i symbols", ncnt);
            cnt += ncnt;
            close(readfd);

            // add to successfully mapped memory region vector
            AddMemoryMapping(itr->start, itr->len);
        }
    }

    // sort function entries to allow effective search
    std::sort(m_symbolTable.begin(), m_symbolTable.end(), FunctionEntrySortPredicate());

    LogFunc(LOG_VERBOSE, "Loaded %i symbols from available sources", cnt);
}

int PerfFile::ResolveSymbolsUsingFD(int fd, uint64_t baseAddress, FunctionEntryType overrideType)
{
    // buffer for reading lines from nm stdout
    char buffer[256];

    // Read from child’s stdout
    int res, pos, cnt;
    char c;
    uint64_t laddr, addrStart;
    char* endptr;
    char fncType;

    cnt = 0;

    // line reading loop - terminated by file end
    while (true)
    {
        pos = 0;
        while ((res = read(fd, &c, sizeof(char))) == 1)
        {
            // stop reading line when end of line character is acquired
            if (c == 10 || c == 13)
                break;

            // read only 255 characters, strip the rest
            if (pos < 255)
                buffer[pos++] = c;
        }

        // this both means end of file - eighter no character was read, or we reached zero character
        if (res <= 0 || c == 0)
            break;

        // properly null-perminate string
        buffer[pos] = 0;

        // require some minimal length, parsing would fail anyway
        if (strlen(buffer) < 8)
            continue;

        // parse address
        laddr = strtoull(buffer, &endptr, 16);
        if (endptr - buffer + 2 > pos)
            break;
        // some symbols may not have address
        if (endptr - buffer < 3)
            continue;

        // resolve function type
        fncType = *(endptr+1);

        addrStart = baseAddress;

        if (fncType != FET_TEXT && fncType != FET_TEXT_2 && fncType != FET_WEAK && fncType != FET_WEAK_2)
            continue;

        if (overrideType == FET_DONTCARE)
        {
            if (fncType == FET_TEXT || fncType == FET_TEXT_2)
                fncType = FET_TEXT;
            else
                fncType = FET_MISC;
        }
        else
            fncType = overrideType;

        // store "the rest of line" as function name to function table
        m_symbolTable.push_back({ laddr + baseAddress, 0, endptr+3, NO_CLASS, (FunctionEntryType)fncType });
        cnt++;
    }

    return cnt;
}

FunctionEntry* PerfFile::GetFunctionByAddress(uint64_t address, uint32_t* functionIndex)
{
    return GetSymbolRecordByAddress(m_functionTable, address, functionIndex);
}

FunctionEntry* PerfFile::GetSymbolByAddress(uint64_t address, uint32_t* functionIndex)
{
    return GetSymbolRecordByAddress(m_symbolTable, address, functionIndex);
}

FunctionEntry* PerfFile::GetSymbolRecordByAddress(std::vector<FunctionEntry> &source, uint64_t address, uint32_t* functionIndex)
{
    if (functionIndex)
        *functionIndex = 0;

    if (source.empty())
        return nullptr;

    // we assume that m_functionTable is sorted from lower address to higher
    // so we are able to perform binary search in O(log(n)) complexity

    uint64_t ilow, ihigh, imid;

    ilow = 0;
    ihigh = source.size() - 1;

    imid = (ilow + ihigh) / 2;

    // iterative binary search
    while (ilow <= ihigh)
    {
        if (source[imid].address > address)
            ihigh = imid - 1;
        else
            ilow = imid + 1;

        imid = (ilow + ihigh) / 2;
    }

    // the outcome may be one step higher (depending from which side we arrived), than we would like to have - we are looking for
    // "highest lower address", i.e. for addresses 2, 5, 10, and input address 7, we return entry with address 5

    if (ilow < source.size() && source[ilow].address <= address)
    {
        if (functionIndex)
            *functionIndex = ilow;
        return &source[ilow];
    }

    if (functionIndex)
        *functionIndex = ilow - 1;

    return &source[ilow - 1];
}

bool PerfFile::ReadAndCheckHeader()
{
    // read header
    if (fread(&m_fileHeader, sizeof(perf_file_header), 1, m_file) != 1)
    {
        LogFunc(LOG_ERROR, "Couldn't read perf file header from supplied file");
        return false;
    }

    // verify magic
    for (int i = 0; i < PERF_FILE_MAGIC_LENGTH; i++)
    {
        if (m_fileHeader.magic[i] != perfFileMagic[i])
        {
            LogFunc(LOG_ERROR, "Supplied file is not perf record file");
            return false;
        }
    }

    return true;
}

bool PerfFile::ReadAttributes()
{
    LogFunc(LOG_VERBOSE, "Reading perf file attributes");

    LogFunc(LOG_DEBUG, "Attributes section size: %llu", m_fileHeader.attrs.size);

    if (m_fileHeader.attrs.offset == 0 && m_fileHeader.attrs.size == 0)
    {
        // TODO: check if this is true (that it's not valid without attributes) and if this may happen at all

        return false;
    }

    // verify attribute struct length
    if (m_fileHeader.attr_size != sizeof(perf_file_attr))
    {
        LogFunc(LOG_ERROR, "Supplied perf file does not have expected attribute section length! (expected: %u, actual: %u)", sizeof(perf_file_attr), m_fileHeader.attr_size);
        // for now, allow different sizes, we need just small portion of it all
        //return false;
    }

    // seek to attrs section
    fseek(m_file, (long)m_fileHeader.attrs.offset, SEEK_SET);

    uint8_t* tmpMem;
    perf_file_attr f_attr;

    // verify attributes size - it has to be divisible to attr structs
    if ((m_fileHeader.attrs.size % sizeof(perf_file_attr)) != 0)
    {
        LogFunc(LOG_ERROR, "Supplied perf file does not have expected attribute section length according to perf_file_attr size!");
        // for now, allow different sizes, we need just small portion of it all
        //return false;
    }

    size_t allocSize = nmax(sizeof(perf_file_attr), m_fileHeader.attr_size);
    size_t scaleSize = nmin(sizeof(perf_file_attr), m_fileHeader.attr_size);

    uint32_t eventAttrCount = (uint32_t)(m_fileHeader.attrs.size / scaleSize);
    m_eventAttr.resize(eventAttrCount);
    m_eventAttrIds.resize(eventAttrCount);

    tmpMem = new uint8_t[allocSize];

    long cur = (long)m_fileHeader.attrs.offset;
    // go through all attributes linked by header
    for (uint32_t i = 0; i < eventAttrCount; i++)
    {
        // read attribute (has to fit the structure)
        if (fread(tmpMem, allocSize, 1, m_file) != 1)
        {
            LogFunc(LOG_ERROR, "Unexpected end of file while reading file attributes section");
            delete tmpMem;
            return false;
        }

        // copy memory to attribute struct
        f_attr = *((perf_file_attr*)tmpMem);

        // store to vector
        m_eventAttr[i].attr = f_attr.attr;

        // this also may not be completely true to require such thing, but it appears
        // to be fine for this case, when we need just regular profiling (for now)
        if (!f_attr.attr.sample_id_all)
        {
            LogFunc(LOG_ERROR, "We need sample_id_all for further parsing!");
            delete tmpMem;
            return false;
        }

        // at first pass, store sampling type; the sampling type has to remain the same
        if (i == 0)
            m_samplingType = f_attr.attr.sample_type;
        else if (m_samplingType != f_attr.attr.sample_type)
        {
            LogFunc(LOG_ERROR, "Sampling type changed during recording, cannot continue");
            delete tmpMem;
            return false;
        }

        uint64_t f_id;
        uint32_t idcount = (uint32_t)(f_attr.ids.size / sizeof(f_id));
        m_eventAttrIds[i].clear();

        // go through all assigned IDs and read them
        if (idcount > 0 && f_attr.ids.size != (uint32_t)(-1))
        {
            cur = ftell(m_file);
            fseek(m_file, (long)f_attr.ids.offset, SEEK_SET);

            for (uint32_t j = 0; j < idcount; j++)
            {
                fread(&f_id, sizeof(f_id), 1, m_file);
                // link sample with attribute section
                m_eventAttrIds[i].insert(f_id);
            }

            // seek back where we came from
            fseek(m_file, cur, SEEK_SET);
        }
    }

    delete tmpMem;

    return true;
}

bool PerfFile::ReadTypes()
{
    LogFunc(LOG_VERBOSE, "Reading perf file event types");

    LogFunc(LOG_DEBUG, "Event types section size: %llu", m_fileHeader.event_types.size);

    // when no event_types section specified, it's still valid
    if (m_fileHeader.event_types.offset == 0 && m_fileHeader.event_types.size == 0)
        return true;

    // seek to event types

    if ((m_fileHeader.event_types.size % sizeof(perf_trace_event_type)) != 0)
    {
        LogFunc(LOG_ERROR, "Supplied perf file does not have expected trace event type section length according to perf_trace_event_type size");
        return false;
    }

    // count of trace blocks
    uint32_t traceInfoCount = (uint32_t)(m_fileHeader.event_types.size / sizeof(perf_trace_event_type));
    m_traceInfo.resize(traceInfoCount);

    // seek there
    fseek(m_file, (long)m_fileHeader.event_types.offset, SEEK_SET);

    bool found;

    // read all trace blocks, one by one
    for (uint32_t i = 0; i < traceInfoCount; i++)
    {
        fread(&m_traceInfo[i], sizeof(perf_trace_event_type), 1, m_file);

        found = false;

        // try to match trace event type to event attribute
        for (size_t j = 0; j < m_eventAttr.size(); j++)
        {
            if (m_eventAttr[j].attr.config == m_traceInfo[i].event_id)
            {
                memcpy(m_eventAttr[j].name, m_traceInfo[i].name, sizeof(m_eventAttr[j].name));
                found = true;
                break;
            }
        }

        // the link is mandatory
        if (!found)
        {
            LogFunc(LOG_ERROR, "Couldn't find matching event attribute structure to trace info");
            return false;
        }
    }

    return true;
}

bool PerfFile::ReadData()
{
    LogFunc(LOG_VERBOSE, "Reading records from perf file");

    LogFunc(LOG_DEBUG, "Data section size: %llu", m_fileHeader.data.size);

    if (m_fileHeader.data.offset == 0 && m_fileHeader.data.size == 0)
    {
        LogFunc(LOG_ERROR, "Specified perf file does not contain any profiling data");
        return false;
    }

    // Following lines are commented out since this is not necessarily true - we ARE able
    // to collect sufficient data from information even without these sampling types,
    // furthermore we will detect the absence of data in analysing phase, since it's not
    // an error at all - just some data in output will be missing
    /*
    uint32_t samplingCriteria = PERF_SAMPLE_TID | PERF_SAMPLE_PERIOD | PERF_SAMPLE_IP;

    if ((m_samplingType & samplingCriteria) != samplingCriteria)
    {
        LogFunc(LOG_ERROR, "Not enough information in perf record file to perform analysis");
        return false;
    }
    */

    fseek(m_file, m_fileHeader.data.offset, SEEK_SET);

    perf_event evt;
    uint8_t* loaded_data = nullptr;
    perf_sample sample;
    uint64_t event_number = 0;
    uint32_t esize, prevsize = 0;
    record_t* rec;

    // while there's still something to be read from data section...
    while (ftell(m_file) < m_fileHeader.data.offset + m_fileHeader.data.size)
    {
        event_number++;

        // read header
        fread(&evt.header, sizeof(perf_event_header), 1, m_file);
        switch (evt.header.type)
        {
            case PERF_RECORD_MMAP:    // mmap record
            case PERF_RECORD_MMAP2:   // mmap record (second type)
            case PERF_RECORD_COMM:    // command record
            case PERF_RECORD_FORK:    // fork record
            case PERF_RECORD_EXIT:    // exit record
            case PERF_RECORD_SAMPLE:  // profiling sample record
            {
                // size of data excluding header
                esize = evt.header.size - sizeof(perf_event_header);

                // if there's need for reallocating the temp space, delete old space
                if (esize > prevsize)
                {
                    // deallocate old memory
                    if (loaded_data != nullptr)
                        delete loaded_data;
                    loaded_data = nullptr;
                }

                // reallocate space if needed
                if (loaded_data == nullptr)
                    loaded_data = new uint8_t[esize];

                // read data
                fread(loaded_data, 1, esize, m_file);

                // assign data to event
                evt._generic = loaded_data;

                // try to parse sample - extract PID, TID, times, generic stuff, and additionally, when
                // it's profiling sample record, extract sample-related stuff like callchains, etc.
                perf_event__parse_sample(&evt, m_samplingType, true, &sample);

                // fill record structure according to specific type, and name for logging purposes
                switch (evt.header.type)
                {
                    case PERF_RECORD_MMAP:
                        rec = create_mmap_msg(evt.mmap);
                        LogFunc(LOG_DEBUG, "mmap, start: 0x%.16llX, length: %llu, file: %s", ((record_mmap*)rec)->start, ((record_mmap*)rec)->len, ((record_mmap*)rec)->filename);
                        break;
                    case PERF_RECORD_MMAP2:
                        rec = create_mmap_msg(evt.mmap);
                        LogFunc(LOG_DEBUG, "mmap2, start: 0x%.16llX, length: %llu, file: %s",
                            ((record_mmap2*)rec)->start, ((record_mmap2*)rec)->len, ((record_mmap2*)rec)->filename);
                        m_mmaps2.push_back((record_mmap2*)rec);
                        break;
                    case PERF_RECORD_COMM:
                        rec = create_comm_msg(evt.comm);
                        LogFunc(LOG_DEBUG, "comm: %s", ((record_comm*)rec)->comm);
                        break;
                    case PERF_RECORD_FORK:
                        rec = create_fork_msg(evt.fork);
                        LogFunc(LOG_DEBUG, "fork, ppid: %u", ((record_fork*)rec)->ppid);
                        break;
                    case PERF_RECORD_EXIT:
                        rec = create_exit_msg(evt.exitev);
                        LogFunc(LOG_DEBUG, "exit, ppid: %u", ((record_exit*)rec)->pid);
                        break;
                    case PERF_RECORD_SAMPLE:
                        rec = create_sample_msg(&sample);
                        // commented out for sanity reasons (for now)
                        //LogFunc(LOG_DEBUG, "sample, ip: %.16llX, period: %llu", ((record_sample*)rec)->ip, ((record_sample*)rec)->period);
                        //for (uint32_t ji = 0; ji < sample.callchain->nr; ji++)
                        //    LogFunc(LOG_DEBUG, "\t%.16llX", sample.callchain->ips[ji]);
                        break;
                }

                // store generic record info
                rec->type = (perf_event_type)evt.header.type;
                rec->nr = event_number - 1;
                rec->time = sample.time;
                rec->cpu = (uint32_t)sample.cpu;
                rec->id = sample.id;

                // store in object storage
                m_records.push_back(rec);

                break;
            }
            default: // unknown or NYI event types
            {
                // just log and seek to next event
                LogFunc(LOG_DEBUG, "Unknown perf record, type: %u; skipped", evt.header.type);
                fseek(m_file, ftell(m_file) + evt.header.size - sizeof(perf_event_header), SEEK_SET);
                continue;
            }
        }
    }

    // deallocate temp space
    if (loaded_data != nullptr)
        delete loaded_data;

    LogFunc(LOG_VERBOSE, "Loaded %llu records from perf file", event_number);

    return true;
}

void PerfFile::FillFunctionTable(std::vector<FunctionEntry> &dst)
{
    LogFunc(LOG_VERBOSE, "Passing function table from input module to core");

    dst.assign(m_functionTable.begin(), m_functionTable.end());
}

void PerfFile::FillFlatProfileTable(std::vector<FlatProfileRecord> &dst)
{
    LogFunc(LOG_VERBOSE, "Passing flat profile table from input module to core");

    dst.assign(m_flatProfile.begin(), m_flatProfile.end());
}

void PerfFile::FillCallGraphMap(CallGraphMap &dst)
{
    LogFunc(LOG_VERBOSE, "Passing call graph from input module to core");

    // perform deep copy
    for (CallGraphMap::iterator itr = m_callGraph.begin(); itr != m_callGraph.end(); ++itr)
        for (std::map<uint32_t, uint64_t>::iterator sitr = itr->second.begin(); sitr != itr->second.end(); ++sitr)
            dst[itr->first][sitr->first] = sitr->second;
}

void PerfFile::FillCallTreeMap(CallTreeMap &dst)
{
    LogFunc(LOG_VERBOSE, "Passing call tree from input module to core");

    // copy just addressess - it will remain the same, do not copy memory contents
    for (CallTreeMap::iterator itr = m_callTree.begin(); itr != m_callTree.end(); ++itr)
        dst[itr->first] = itr->second;
}

void PerfFile::FillHeatMapData(TimeHistogramVector &dst)
{
    LogFunc(LOG_VERBOSE, "Passing heat map data from input module to core");

    // determine time range to properly scale time segment vector
    uint64_t minTime = (uint64_t)(-1);
    uint64_t maxTime = 0;

    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            if (itr->time > maxTime)
                maxTime = itr->time;
            if (itr->time < minTime)
                minTime = itr->time;
        }
    }

    // scale down to milliseconds
    uint64_t timeRange = (maxTime - minTime) / SAMPLE_TIMESTAMP_DIMENSION_TO_MS;

    uint64_t binCount = 1 + (timeRange / HEATMAP_GROUP_BY_MS_AMOUNT);

    LogFunc(LOG_VERBOSE, "Heat map contains %llu bins", binCount);

    dst.resize(binCount);

    record_sample* sample;
    FunctionEntry* fet;

    uint32_t findex, childindex;
    uint64_t binidx;
    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            sample = ((record_sample*)itr);

            fet = GetFunctionByAddress(sample->ip, &findex);
            // for now, exclude kernel symbols (for sanity reasons)
            if (fet && fet->functionType != FET_KERNEL)
            {
                binidx = ((itr->time - minTime) / SAMPLE_TIMESTAMP_DIMENSION_TO_MS) / HEATMAP_GROUP_BY_MS_AMOUNT;

                if (dst[binidx].find(findex) == dst[binidx].end())
                {
                    dst[binidx][findex].timeTotal = 0;
                    dst[binidx][findex].timeTotalInclusive = 0;
                }

                dst[binidx][findex].timeTotal += 1.0;
                dst[binidx][findex].timeTotalInclusive += 1.0;

                // now calculate inclusive time

                // 2 is the right value, since IP callchain contains invalid address ("stopper") on top
                // and self as second record
                for (uint64_t i = 2; i < sample->callchain->nr; i++)
                {
                    fet = GetFunctionByAddress(sample->callchain->ips[i], &childindex);
                    // also exclude kernel calls for now
                    if (fet && fet->functionType != FET_KERNEL)
                    {
                        if (dst[binidx].find(childindex) == dst[binidx].end())
                        {
                            dst[binidx][childindex].timeTotal = 0;
                            dst[binidx][childindex].timeTotalInclusive = 0;
                        }

                        dst[binidx][childindex].timeTotalInclusive += 1.0;
                    }
                }
            }
        }
    }
}
