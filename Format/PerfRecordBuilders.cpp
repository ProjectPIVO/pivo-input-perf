#include "General.h"
#include "PerfFileStructs.h"

int perf_event__parse_id_sample(perf_event *event, uint64_t type, perf_sample *sample)
{
    uint64_t *array = &event->sample->array;

    array += ((event->header.size - sizeof(event->header)) / sizeof(uint64_t)) - 1;

    if (type & PERF_SAMPLE_CPU)
    {
        uint32_t *p = (uint32_t*)array;
        sample->cpu = *p;
        array--;
    }

    if (type & PERF_SAMPLE_STREAM_ID)
    {
        sample->stream_id = *array;
        array--;
    }

    if (type & PERF_SAMPLE_ID)
    {
        sample->id = *array;
        array--;
    }

    if (type & PERF_SAMPLE_TIME)
    {
        sample->time = *array;
        array--;
    }

    if (type & PERF_SAMPLE_TID)
    {
        uint32_t *p = (uint32_t*)array;
        sample->pid = p[0];
        sample->tid = p[1];
    }

    return 0;
}

int perf_event__parse_sample(perf_event *event, uint64_t type, bool sample_id_all, perf_sample *data)
{
    uint64_t *array;

    data->cpu = -1;
    data->pid = -1;
    data->tid = -1;
    data->stream_id = -1;
    data->id = -1;
    data->time = -1;

    if (event->header.type != PERF_RECORD_SAMPLE)
    {
        if (!sample_id_all)
            return 0;
        return perf_event__parse_id_sample(event, type, data);
    }

    array = &event->sample->array;

    if (type & PERF_SAMPLE_IP)
    {
        data->ip = event->ip->ip;
        array++;
    }

    if (type & PERF_SAMPLE_TID)
    {
        uint32_t *p = (uint32_t*)array;
        data->pid = p[0];
        data->tid = p[1];
        array++;
    }

    if (type & PERF_SAMPLE_TIME)
    {
        data->time = *array;
        array++;
    }

    if (type & PERF_SAMPLE_ADDR)
    {
        data->addr = *array;
        array++;
    }

    data->id = (uint64_t)(-1);
    if (type & PERF_SAMPLE_ID)
    {
        data->id = *array;
        array++;
    }

    if (type & PERF_SAMPLE_STREAM_ID)
    {
        data->stream_id = *array;
        array++;
    }

    if (type & PERF_SAMPLE_CPU)
    {
        uint32_t *p = (uint32_t*)array;
        data->cpu = *p;
        array++;
    }

    if (type & PERF_SAMPLE_PERIOD)
    {
        data->period = *array;
        array++;
    }

    if (type & PERF_SAMPLE_READ)
    {
        // unsupported
        return -1;
    }

    if (type & PERF_SAMPLE_CALLCHAIN)
    {
        data->callchain = new ip_callchain;
        uint32_t count = (uint32_t)((ip_callchain*)array)->nr;

        data->callchain->nr = count;
        data->callchain->ips = new uint64_t[count];
        for (uint64_t it = 0; it < count; it++)
            data->callchain->ips[it] = ( (uint64_t*)&((ip_callchain*)array)->ips )[it];

        array += 1 + count;
    }

    if (type & PERF_SAMPLE_RAW)
    {
        uint32_t *p = (uint32_t*)array;
        data->raw_size = *p;
        p++;
        data->raw_data = p;
    }

    return 0;
}

record_t* create_mmap_msg(mmap_event *evt)
{
    record_mmap* rec = new record_mmap;
    rec->header.pid = evt->pid;
    rec->header.tid = evt->tid;
    rec->start = evt->start;
    rec->len = evt->len;
    rec->pgoff = evt->pgoff;
    memcpy(rec->filename, evt->filename, sizeof(rec->filename));
    return &rec->header;
}

record_t* create_mmap2_msg(mmap2_event *evt)
{
    record_mmap2* rec = new record_mmap2;
    rec->header.pid = evt->pid;
    rec->header.tid = evt->tid;
    rec->start = evt->start;
    rec->len = evt->len;
    rec->pgoff = evt->pgoff;
    rec->major = evt->major;
    rec->minor = evt->minor;
    rec->ino = evt->ino;
    rec->ino_gen = evt->ino_gen;
    rec->prot = evt->prot;
    rec->flags = evt->flags;
    memcpy(rec->filename, evt->filename, sizeof(rec->filename));
    return &rec->header;
}

record_t* create_comm_msg(comm_event *evt)
{
    record_comm* rec = new record_comm;
    rec->header.pid = evt->pid;
    rec->header.tid = evt->tid;
    memcpy(rec->comm, evt->comm, sizeof(rec->comm));
    return &rec->header;
}

record_t* create_fork_msg(fork_event *evt)
{
    record_fork* rec = new record_fork;
    rec->header.pid = evt->pid;
    rec->header.tid = evt->tid;
    rec->ppid = evt->ppid;
    rec->ptid = evt->ptid;
    return &rec->header;
}

record_t* create_exit_msg(exit_event *evt)
{
    record_exit* rec = new record_exit;
    rec->header.pid = evt->pid;
    rec->header.tid = evt->tid;
    rec->pid = evt->ppid;
    rec->tid = evt->ptid;
    return &rec->header;
}

record_t* create_sample_msg(perf_sample *evt)
{
    record_sample* rec = new record_sample;
    rec->header.pid = evt->pid;
    rec->header.tid = evt->tid;
    rec->header.cpu = (uint32_t)evt->cpu;
    rec->header.id = evt->id;
    rec->ip = evt->ip;
    rec->period = evt->period;
    rec->callchain = new ip_callchain;

    rec->callchain->nr = evt->callchain->nr;
    rec->callchain->ips = new uint64_t[rec->callchain->nr];
    memcpy(rec->callchain->ips, evt->callchain->ips, rec->callchain->nr * sizeof(uint64_t));

    return &rec->header;
}
