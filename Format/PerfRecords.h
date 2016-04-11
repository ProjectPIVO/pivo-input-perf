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

#ifndef PIVO_PERF_RECORDS_H
#define PIVO_PERF_RECORDS_H

#define MAX_PERF_EVENT_NAME 64

// taken from linux/limits.h, aligned to fit perf format
#define UX_PATH_MAX 4096

#include "linux/perf_event.h"

struct perf_event;
struct perf_sample;
struct mmap_event;
struct mmap2_event;
struct comm_event;
struct fork_event;
struct exit_event;
struct ip_callchain;

struct record_t
{
    uint32_t pid;
    uint32_t tid;
    uint32_t cpu;
    uint64_t time;
    uint64_t nr;
    uint64_t id;
    perf_event_type type;
};

struct record_mmap
{
    record_t header;
    uint64_t start;
    uint64_t len;
    uint64_t pgoff;
    char filename[UX_PATH_MAX];
};

struct record_mmap2
{
    record_t header;
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

struct record_comm
{
    record_t header;
    char comm[16];
};

struct record_fork
{
    record_t header;
    uint32_t ppid;
    uint32_t ptid;
};

struct record_exit
{
    record_t header;
    uint32_t pid;
    uint32_t tid;
};

struct record_sample
{
    record_t header;
    uint64_t ip;
    uint64_t period;
    ip_callchain* callchain;
};

int perf_event__parse_id_sample(perf_event *event, uint64_t type, perf_sample *sample);
int perf_event__parse_sample(perf_event *event, uint64_t type, bool sample_id_all, perf_sample *data);

record_t* create_mmap_msg(mmap_event *evt);
record_t* create_mmap2_msg(mmap2_event *evt);
record_t* create_comm_msg(comm_event *evt);
record_t* create_fork_msg(fork_event *evt);
record_t* create_exit_msg(exit_event *evt);
record_t* create_sample_msg(perf_sample *evt);

#endif
