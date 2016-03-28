#ifndef PIVO_PERF_FILE_H
#define PIVO_PERF_FILE_H

#include "PerfFileStructs.h"

#include "UnitIdentifiers.h"
#include "FlatProfileStructs.h"
#include "CallGraphStructs.h"
#include "CallTreeStructs.h"

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

typedef std::pair<uint64_t, uint64_t> MemoryRegion;
typedef std::vector<MemoryRegion> MemoryRegionVector;

class PerfFile
{
    public:
        // static factory method for loading perf recorded file
        static PerfFile* Load(const char* filename, const char* binaryfilename);

        // fills function table with symbol info resolved
        void FillFunctionTable(std::vector<FunctionEntry> &dst);
        // fills flat profile table
        void FillFlatProfileTable(std::vector<FlatProfileRecord> &dst);
        // fills call graph map with gathered data
        void FillCallGraphMap(CallGraphMap &dst);
        // fills call tree map with gathered data
        void FillCallTreeMap(CallTreeMap &dst);

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
        // filter symbols - use only sampled ones (the ones present in perf file)
        void FilterUsedSymbols();

        // retrieves function entry based on input address
        FunctionEntry* GetSymbolRecordByAddress(std::vector<FunctionEntry> &source, uint64_t address, uint32_t* functionIndex);
        // retrieves function entry based on input address
        FunctionEntry* GetFunctionByAddress(uint64_t address, uint32_t* functionIndex);
        // retrieves function entry based on input address
        FunctionEntry* GetSymbolByAddress(uint64_t address, uint32_t* functionIndex);

        // processess flat profile from available data
        void ProcessFlatProfile();
        // creates call graph map
        void ProcessCallGraph();
        // creates call tree
        void ProcessCallTree();

        // creates empty an nullified call tree node
        CallTreeNode* CreateCallTreeNode(uint32_t functionId, CallTreeNode* childOf = nullptr);
        // inserts node (if needed) into call tree and returns CallTreeNode instance
        CallTreeNode* InsertIntoCallTree(std::vector<uint32_t> &path, uint32_t finalFunctionId);
        // adds time to whole call chain
        void AccumulateCallTreeTime(CallTreeNode* node, double addTime, bool addSample = false);

        // Process mmap and mmap2 samples and add appropriate ranges to search arrays
        void ProcessMemoryMapping();
        // adds memory mapping (mmap or mmap2) to known address ranges
        void AddMemoryMapping(uint64_t start, uint64_t length);
        // determines the presence of address in known region map
        bool IsWithinMemoryMapping(uint64_t address);

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

        // vector of memory region mappings (via mmap or mmap2)
        MemoryRegionVector m_memoryMappings;

        // table of addresses of functions
        std::vector<FunctionEntry> m_functionTable;
        // table of symbols found
        std::vector<FunctionEntry> m_symbolTable;

        // table of flat profile records
        std::vector<FlatProfileRecord> m_flatProfile;
        // call graph map
        CallGraphMap m_callGraph;
        // call tree set (root nodes)
        CallTreeMap m_callTree;
};

#endif
