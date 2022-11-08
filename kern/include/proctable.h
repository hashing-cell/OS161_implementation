#ifndef _PROCTABLE_H
#define _PROCTABLE_H

#include <types.h>
#include <limits.h>
#include <lib.h>
#include <proc.h>

struct proctable {
    struct proc* proc_entries[PID_MAX];
    unsigned num_processes;
    unsigned next_pid;
    struct lock* lk_pt;
};

//initialize this before proc_bootstrap
void proctable_bootstrap(void);

#endif