#include "General.h"
#include "InputModule.h"
#include "PerfInputModule.h"
#include "PerfFile.h"
#include "PerfRecords.h"
#include "Log.h"

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

    // TODO: analyze data, read functions table (nm, /proc/kallsyms, misc dbg info from libraries from ldd output), etc.

    fclose(pf);

    return pfile;
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
    void* loaded_data = nullptr;
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
                        LogFunc(LOG_DEBUG, "mmap, start: %.16llX, length: %llu", ((record_mmap*)rec)->start, ((record_mmap*)rec)->len);
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
                        //for (ji = 0; ji < sample.callchain->nr; ji++)
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
