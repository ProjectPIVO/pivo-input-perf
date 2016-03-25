#ifndef PIVO_PERF_FILE_H
#define PIVO_PERF_FILE_H

#include "PerfFileStructs.h"

#include "UnitIdentifiers.h"
#include "FlatProfileStructs.h"

#include <set>

// currently supported perf file version is 2 (magic PERFILE2)
const char perfFileMagic[PERF_FILE_MAGIC_LENGTH] = { 'P', 'E', 'R', 'F', 'I', 'L', 'E', '2' };

// structure used for sorting std::vector of FlatProfileRecord by time spent
struct PerfRecordTimeSortPredicate
{
    inline bool operator() (const record_t* a, const record_t* b)
    {
        return (a->time < b->time);
    }
};

class PerfFile
{
    public:
        // static factory method for loading perf recorded file
        static PerfFile* Load(const char* filename, const char* binaryfilename);

        // fills function table with symbol info resolved
        void FillFunctionTable(std::vector<FunctionEntry> &dst);
        // fills flat profile table
        void FillFlatProfileTable(std::vector<FlatProfileRecord> &dst);

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

        // resolve symbols from supplied file
        void ResolveSymbols(const char* binaryFilename);
        // use specified file descriptor to resolve symbols from (in nm format)
        int ResolveSymbolsUsingFD(int fd, uint64_t baseAddress = 0x0, FunctionEntryType overrideType = FET_DONTCARE);

        // retrieves function entry based on input address
        FunctionEntry* GetFunctionByAddress(uint64_t address, uint32_t* functionIndex);

        // processess flat profile from available data
        void ProcessFlatProfile();

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
        std::vector<record_t*> m_records;
        // stored mmap2 events (to resolve symbols later)
        std::vector<record_mmap2*> m_mmaps2;

        // table of addresses of functions
        std::vector<FunctionEntry> m_functionTable;

        // table of flat profile records
        std::vector<FlatProfileRecord> m_flatProfile;
};

#endif
