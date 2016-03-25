#include "General.h"
#include "InputModule.h"
#include "PerfInputModule.h"
#include "PerfFile.h"
#include "PerfRecords.h"
#include "Log.h"
#include "Helpers.h"
#include "../config_perf.h"

#include <algorithm>

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

    // TODO: analyze data, read functions table (nm, /proc/kallsyms, misc dbg info from libraries from ldd output), etc.

    pfile->ProcessFlatProfile();

    fclose(pf);

    return pfile;
}

void PerfFile::ProcessFlatProfile()
{
    m_flatProfile.resize(m_functionTable.size());

    FlatProfileRecord *fp;

    // prepare flat profile table, it will match function table at first stage of filling
    for (int i = 0; i < m_functionTable.size(); i++)
    {
        fp = &m_flatProfile[i];

        fp->functionId = i;
        fp->callCount = 0;
        fp->timeTotal = 0;
        fp->timeTotalPct = 0.0f;
    }

    uint64_t lastTime = 0;
    uint32_t lastFindex = 0;
    double baseTime;

    uint32_t findex;
    for (record_t* itr : m_records)
    {
        if (itr->type == PERF_RECORD_SAMPLE)
        {
            record_sample* sample = ((record_sample*)itr);
            FunctionEntry* fet;

            fet = GetFunctionByAddress(sample->ip, &findex);
            if (fet /*&& fet->functionType != FET_KERNEL*/)
            {
                if (lastTime != 0)
                {
                    // time in perf file format is in nanoseconds (based on system uptime, but
                    // that is not important, since we need only relative differences)
                    baseTime = ( ((double)(sample->header.time - lastTime)) / 2.0 ) / 1000000000.0;
                    m_flatProfile[findex].timeTotal += baseTime;
                    m_flatProfile[lastFindex].timeTotal += baseTime;
                }

                lastTime = sample->header.time;
                lastFindex = findex;

                m_flatProfile[findex].callCount++;
            }
        }
    }
}

void PerfFile::ResolveSymbols(const char* binaryFilename)
{
    int cnt = 0;

    LogFunc(LOG_VERBOSE, "Loading debug symbols from application binary...");

    // build nm binary call parameters
    const char *argv[] = {NM_BINARY_PATH, "-a", "-C", binaryFilename, 0};

    // retrieve symbols from binary file
    int readfd = ForkProcessForReading(argv);
    if (readfd > 0)
    {
        cnt += ResolveSymbolsUsingFD(readfd);
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
        cnt += ResolveSymbolsUsingFD(readfd, 0, FET_KERNEL);
        fclose(kallsymfile);
    }
    else
        LogFunc(LOG_ERROR, "Could not load kernel symbols from /proc/kallsyms");

    LogFunc(LOG_VERBOSE, "Loading debug symbols from dynamically linked libraries...");

    std::string libpath;
    // go through all mmap'd records (via mmap2) and try to load debug libraries connected with them
    for (record_mmap2* itr : m_mmaps2)
    {
        // TODO: find more portable way to look for debug library path
        // by default, Debian-based systems stores such libs in /usr/lib/debug
        libpath = "/usr/lib/debug";
        libpath += itr->filename;

        FILE* tst = fopen(libpath.c_str(), "r");
        if (!tst)
            continue;

        fclose(tst);

        LogFunc(LOG_VERBOSE, "Loading debug symbols from %s...", libpath.c_str());

        // substitute binary parameter in "nm" command line
        argv[3] = libpath.c_str();

        readfd = ForkProcessForReading(argv);
        if (readfd > 0)
        {
            // the base address is mandatory here, since the memory is mmap'd to
            // another offset in virtual address space, thus all symbols are
            // moved by this offset
            cnt += ResolveSymbolsUsingFD(readfd, itr->start);
            close(readfd);
        }
    }

    // sort function entries to allow effective search
    std::sort(m_functionTable.begin(), m_functionTable.end(), FunctionEntrySortPredicate());

    LogFunc(LOG_VERBOSE, "Loaded %i symbols from available sources", cnt);
}

int PerfFile::ResolveSymbolsUsingFD(int fd, uint64_t baseAddress, FunctionEntryType overrideType)
{
    // buffer for reading lines from nm stdout
    char buffer[256];

    // Read from child’s stdout
    int res, pos, cnt;
    char c;
    uint64_t laddr;
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

        // resolve function type
        fncType = *(endptr+1);
        if (overrideType == FET_DONTCARE)
        {
            if (fncType == 'T' || fncType == 't')
                fncType = FET_TEXT;
            else
                fncType = FET_MISC;
        }
        else
            fncType = overrideType;

        // store "the rest of line" as function name to function table
        m_functionTable.push_back({ laddr + baseAddress, 0, endptr+3, NO_CLASS, (FunctionEntryType)fncType });
        cnt++;
    }

    return cnt;
}

FunctionEntry* PerfFile::GetFunctionByAddress(uint64_t address, uint32_t* functionIndex)
{
    if (functionIndex)
        *functionIndex = 0;

    if (m_functionTable.empty())
        return nullptr;

    // we assume that m_functionTable is sorted from lower address to higher
    // so we are able to perform binary search in O(log(n)) complexity

    uint64_t ilow, ihigh, imid;

    ilow = 0;
    ihigh = m_functionTable.size() - 1;

    imid = (ilow + ihigh) / 2;

    // iterative binary search
    while (ilow <= ihigh)
    {
        if (m_functionTable[imid].address > address)
            ihigh = imid - 1;
        else
            ilow = imid + 1;

        imid = (ilow + ihigh) / 2;
    }

    // the outcome may be one step higher (depending from which side we arrived), than we would like to have - we are looking for
    // "highest lower address", i.e. for addresses 2, 5, 10, and input address 7, we return entry with address 5

    if (m_functionTable[ilow].address <= address)
    {
        if (functionIndex)
            *functionIndex = ilow;
        return &m_functionTable[ilow];
    }

    if (functionIndex)
        *functionIndex = ilow - 1;

    return &m_functionTable[ilow - 1];
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
    if (m_fileHeader.attrs.offset == 0 && m_fileHeader.attrs.size == 0)
    {
        // TODO: check if this is true (that it's not valid without attributes) and if this may happen at all

        return false;
    }

    // verify attribute struct length
    if (m_fileHeader.attr_size != sizeof(perf_file_attr))
    {
        LogFunc(LOG_ERROR, "Supplied perf file does not have expected attribute section length");
        return false;
    }

    // seek to attrs section
    fseek(m_file, (long)m_fileHeader.attrs.offset, SEEK_SET);

    perf_file_attr f_attr;

    // verify attributes size - it has to be divisible to attr structs
    if ((m_fileHeader.attrs.size % sizeof(perf_file_attr)) != 0)
    {
        LogFunc(LOG_ERROR, "Supplied perf file does not have expected attribute section length according to perf_file_attr size");
        return false;
    }

    uint32_t eventAttrCount = (uint32_t)(m_fileHeader.attrs.size / sizeof(perf_file_attr));
    m_eventAttr.resize(eventAttrCount);
    m_eventAttrIds.resize(eventAttrCount);

    long cur = (long)m_fileHeader.attrs.offset;
    // go through all attributes linked by header
    for (uint32_t i = 0; i < eventAttrCount; i++)
    {
        // read attribute (has to fit the structure)
        if (fread(&f_attr, sizeof(perf_file_attr), 1, m_file) != 1)
        {
            LogFunc(LOG_ERROR, "Unexpected end of file while reading file attributes section");
            return false;
        }

        // store to vector
        m_eventAttr[i].attr = f_attr.attr;

        // this also may not be completely true to require such thing, but it appears
        // to be fine for this case, when we need just regular profiling (for now)
        if (!f_attr.attr.sample_id_all)
        {
            LogFunc(LOG_ERROR, "We need sample_id_all for further parsing!");
            return false;
        }

        // at first pass, store sampling type; the sampling type has to remain the same
        if (i == 0)
            m_samplingType = f_attr.attr.sample_type;
        else if (m_samplingType != f_attr.attr.sample_type)
        {
            LogFunc(LOG_ERROR, "Sampling type changed during recording, cannot continue");
            return false;
        }

        uint64_t f_id;
        uint32_t idcount = (uint32_t)(f_attr.ids.size / sizeof(f_id));
        m_eventAttrIds[idcount].clear();

        // go through all assigned IDs and read them
        if (idcount > 0)
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

    return true;
}

bool PerfFile::ReadTypes()
{
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
    if (m_fileHeader.data.offset == 0 && m_fileHeader.data.size == 0)
    {
        LogFunc(LOG_ERROR, "Specified perf file does not contain any profiling data");
        return false;
    }

    // Following lines are commented out since this is not necessarily true - we ARE able
    // to collect sufficient data from informations even without these sampling types,
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

    perf_event evt;
    uint8_t* loaded_data = nullptr;
    perf_sample sample;
    uint64_t event_number = 0;
    uint32_t esize, prevsize = 0;

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

                record_t* rec;

                std::string name;
                // fill record structure according to specific type, and name for logging purposes
                switch (evt.header.type)
                {
                    case PERF_RECORD_MMAP:
                        name = "mmap";
                        rec = create_mmap_msg(evt.mmap);
                        LogFunc(LOG_DEBUG, "mmap, start: 0x%.16llX, length: %llu, file: %s", ((record_mmap*)rec)->start, ((record_mmap*)rec)->len, ((record_mmap*)rec)->filename);
                        break;
                    case PERF_RECORD_MMAP2:
                        name = "mmap2";
                        rec = create_mmap_msg(evt.mmap);
                        LogFunc(LOG_DEBUG, "mmap2, start: 0x%.16llX, length: %llu, file: %s",
                            ((record_mmap2*)rec)->start, ((record_mmap2*)rec)->len, ((record_mmap2*)rec)->filename);
                        m_mmaps2.push_back((record_mmap2*)rec);
                        break;
                    case PERF_RECORD_COMM:
                        name = "comm";
                        rec = create_comm_msg(evt.comm);
                        LogFunc(LOG_DEBUG, "comm: %s", ((record_comm*)rec)->comm);
                        break;
                    case PERF_RECORD_FORK:
                        name = "fork";
                        rec = create_fork_msg(evt.fork);
                        LogFunc(LOG_DEBUG, "fork, ppid: %u", ((record_fork*)rec)->ppid);
                        break;
                    case PERF_RECORD_EXIT:
                        name = "exit";
                        rec = create_exit_msg(evt.exitev);
                        LogFunc(LOG_DEBUG, "exit, ppid: %u", ((record_exit*)rec)->pid);
                        break;
                    case PERF_RECORD_SAMPLE:
                        name = "sample";
                        rec = create_sample_msg(&sample);
                        // commented out for sanity reasons (for now)
                        //LogFunc(LOG_DEBUG, "sample, ip: %.16llX, period: %llu", ((record_sample*)rec)->ip, ((record_sample*)rec)->period);
                        //for (uint32_t ji = 0; ji < sample.callchain->nr; ji++)
                        //    LogFunc(LOG_DEBUG, "\t%.16llX", sample.callchain->ips[ji]);
                        break;
                }

                //LogFunc(LOG_DEBUG, "Read '%s' perf event", name.c_str());

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
                LogFunc(LOG_DEBUG, "unknown, type: %u; skipped", evt.header.type);
                fseek(m_file, ftell(m_file) + evt.header.size - sizeof(perf_event_header), SEEK_SET);
                continue;
            }
        }
    }

    // deallocate temp space
    if (loaded_data != nullptr)
        delete loaded_data;

    return true;
}

void PerfFile::FillFunctionTable(std::vector<FunctionEntry> &dst)
{
    dst.assign(m_functionTable.begin(), m_functionTable.end());
}

void PerfFile::FillFlatProfileTable(std::vector<FlatProfileRecord> &dst)
{
    dst.assign(m_flatProfile.begin(), m_flatProfile.end());
}
