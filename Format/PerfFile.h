#ifndef PIVO_PERF_FILE_H
#define PIVO_PERF_FILE_H

#include "PerfFileStructs.h"

#include <set>

// currently supported perf file version is 2 (magic PERFILE2)
const char perfFileMagic[PERF_FILE_MAGIC_LENGTH] = { 'P', 'E', 'R', 'F', 'I', 'L', 'E', '2' };

class PerfFile
{
    public:
        // static factory method for loading perf recorded file
        static PerfFile* Load(const char* filename, const char* binaryfilename);

    protected:
        // private constructor; use PerfFile::Load to instantiate this class
        PerfFile();

        // reads header and checks validity
        bool ReadAndCheckHeader();
        // reads attributes section (needs to have header read first)
        bool ReadAttributes();
        // reads types section (needs to have header read first)
        bool ReadTypes();
        // reads data section (needs to have header read first)
        bool ReadData();

    private:
        // perf file we read
        FILE* m_file;

        // stored sampling type (which data we could extract from perf record file)
        uint64_t m_samplingType;

        // header read from file
        perf_file_header m_fileHeader;
        // all event attributes
        std::vector<event_type_entry> m_eventAttr;
        // assigned ids for each event attribute block
        std::vector< std::set<uint64_t> > m_eventAttrIds;
        // trace info blocks
        std::vector<perf_trace_event_type> m_traceInfo;
        // stored loaded records (events from data section)
        std::list<record_t*> m_records;
};

#endif
