#ifndef PIVO_PERF_FILE_STRUCTS_H
#define PIVO_PERF_FILE_STRUCTS_H

#include "PerfRecords.h"

#define PERF_FILE_MAGIC_LENGTH 8
#define HEADER_FEATURE_BITS 256

#include "linux/perf_event.h"

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

struct mmap2_event
{
    uint32_t pid;
    uint32_t tid;
    uint64_t start;
    uint64_t len;
    uint64_t pgoff;
    uint32_t major;
    uint32_t minor;
    uint64_t ino;
    uint64_t ino_gen;
    uint32_t prot;
    uint32_t flags;
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
        mmap2_event *mmap2;
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
