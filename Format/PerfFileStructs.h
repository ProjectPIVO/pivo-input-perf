#ifndef PIVO_PERF_FILE_STRUCTS_H
#define PIVO_PERF_FILE_STRUCTS_H

#include "PerfRecords.h"

#define PERF_FILE_MAGIC_LENGTH 8
#define HEADER_FEATURE_BITS 256

// perf event sample type enumerator (bitmask fields)
enum perf_event_sample_format
{
    PERF_SAMPLE_IP = 1 << 0,
    PERF_SAMPLE_TID = 1 << 1,
    PERF_SAMPLE_TIME = 1 << 2,
    PERF_SAMPLE_ADDR = 1 << 3,
    PERF_SAMPLE_READ = 1 << 4,
    PERF_SAMPLE_CALLCHAIN = 1 << 5,
    PERF_SAMPLE_ID = 1 << 6,
    PERF_SAMPLE_CPU = 1 << 7,
    PERF_SAMPLE_PERIOD = 1 << 8,
    PERF_SAMPLE_STREAM_ID = 1 << 9,
    PERF_SAMPLE_RAW = 1 << 10,
    PERF_SAMPLE_BRANCH_STACK = 1 << 11,
    PERF_SAMPLE_REGS_USER = 1 << 12,
    PERF_SAMPLE_STACK_USER = 1 << 13,
    PERF_SAMPLE_WEIGHT = 1 << 14,
    PERF_SAMPLE_DATA_SRC = 1 << 15,
    PERF_SAMPLE_IDENTIFIER = 1 << 16,
    PERF_SAMPLE_TRANSACTION = 1 << 17,
    PERF_SAMPLE_REGS_INTR = 1 << 18,
    PERF_SAMPLE_MAX = 1 << 19
};

enum perf_event_type
{
    PERF_RECORD_MMAP = 1,
    PERF_RECORD_LOST = 2,
    PERF_RECORD_COMM = 3,
    PERF_RECORD_EXIT = 4,

    PERF_RECORD_FORK = 7,
    PERF_RECORD_SAMPLE = 9
};

// taken from linux/perf_event.h
struct perf_event_attr
{
    uint32_t type;
    uint32_t size;
    uint64_t config;

    union
    {
        uint64_t sample_period;
        uint64_t sample_freq;
    };

    uint64_t sample_type;
    uint64_t read_format;

    uint64_t    disabled : 1,
                inherit : 1,
                pinned : 1,
                exclusive : 1,
                exclude_user : 1,
                exclude_kernel : 1,
                exclude_hv : 1,
                exclude_idle : 1,
                mmap : 1,
                comm : 1,
                freq : 1,
                inherit_stat : 1,
                enable_on_exec : 1,
                task : 1,
                watermark : 1,
                precise_ip : 2,
                mmap_data : 1,
                sample_id_all : 1,
                exclude_host : 1,
                exclude_guest : 1,
                exclude_callchain_kernel : 1,
                exclude_callchain_user : 1,
                mmap2 : 1,
                comm_exec : 1,
                __reserved_1: 39;

    union
    {
        uint32_t wakeup_events;
        uint32_t wakeup_watermark;
    };

    uint32_t bp_type;

    union
    {
        uint64_t bp_addr;
        uint64_t config1;
    };

    union
    {
        uint64_t bp_len;
        uint64_t config2;
    };

    uint64_t branch_sample_type;
    uint64_t sample_regs_user;
    uint32_t sample_stack_user;
    uint32_t __reserved_2;
    uint64_t __reserver_3__DELETEME; // this field is stuffed in just to fill hole in format specification
                                     // i am not completelly sure, what's missing
};

// taken from linux/perf_event.h
struct perf_event_header
{
    uint32_t type;
    uint16_t misc;
    uint16_t size;
};

struct perf_file_section
{
    uint64_t offset;
    uint64_t size;
};

struct perf_file_attr
{
    perf_event_attr attr;
    perf_file_section ids;
};

struct perf_file_header
{
    char magic[PERF_FILE_MAGIC_LENGTH];
    uint64_t size;
    uint64_t attr_size;
    perf_file_section attrs;
    perf_file_section data;
    perf_file_section event_types;
    uint8_t bits[HEADER_FEATURE_BITS / 8];
};

struct event_type_entry
{
    perf_event_attr attr;
    char name[MAX_PERF_EVENT_NAME];
};

struct perf_trace_event_type
{
    uint64_t event_id;
    char name[MAX_PERF_EVENT_NAME];
};

// sample record structures

struct ip_event
{
    uint64_t ip;
    uint32_t pid;
    uint32_t tid;
    unsigned char* __more_data;
};

struct mmap_event
{
    uint32_t pid;
    uint32_t tid;
    uint64_t start;
    uint64_t len;
    uint64_t pgoff;
    char filename[UX_PATH_MAX];
};

struct comm_event
{
    uint32_t pid;
    uint32_t tid;
    char comm[16];
};

struct fork_event
{
    uint32_t pid;
    uint32_t ppid;
    uint32_t tid;
    uint32_t ptid;
    uint64_t time;
};

struct exit_event
{
    uint32_t pid;
    uint32_t ppid;
    uint32_t tid;
    uint32_t ptid;
    uint64_t time;
};

struct lost_event
{
    uint64_t id;
    uint64_t lost;
};

struct read_event
{
    uint32_t pid;
    uint32_t tid;
    uint64_t value;
    uint64_t time_enabled;
    uint64_t time_running;
    uint64_t id;
};

struct sample_event
{
    uint64_t array; // this just serves as default start point for following data
};

struct ip_callchain
{
    uint64_t nr;
    uint64_t* ips;
};

struct perf_sample
{
    uint64_t ip;
    uint32_t pid;
    uint32_t tid;
    uint64_t time;
    uint64_t addr;
    uint64_t id;
    uint64_t stream_id;
    uint64_t period;
    uint64_t cpu;
    uint64_t raw_size;
    void *raw_data;
    ip_callchain *callchain;
};

enum perf_user_event_type
{
    PERF_RECORD_USER_TYPE_START = 64,
    PERF_RECORD_HEADER_ATTR = 64,
    PERF_RECORD_HEADER_EVENT_TYPE = 65,
    PERF_RECORD_HEADER_TRACING_DATA = 66,
    PERF_RECORD_HEADER_BUILD_ID = 67,
    PERF_RECORD_FINISHED_ROUND = 68,
    PERF_RECORD_HEADER_MAX
};

struct attr_event
{
    perf_event_attr attr;
    uint64_t* id;
};

struct event_type_event
{
    perf_trace_event_type event_type;
};

struct tracing_data_event
{
    uint32_t size;
};

// generic perf_event structure
struct perf_event
{
    perf_event_header header;
    union
    {
        void* _generic;
        ip_event *ip;
        mmap_event *mmap;
        comm_event *comm;
        fork_event *fork;
        exit_event *exitev;
        sample_event *sample;

        // Not yet supported:

        //lost_event *lost;
        //read_event *read;
        //attr_event *attr;
        //event_type_event *event_type;
        //tracing_data_event *tracing_data;
    };
};


#endif
