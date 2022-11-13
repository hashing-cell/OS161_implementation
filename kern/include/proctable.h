#ifndef _PROCTABLE_H
#define _PROCTABLE_H

#include <types.h>
#include <limits.h>
#include <lib.h>
#include <proc.h>
#include <synch.h>
#include <kern/errno.h>

#define NO_PID 0
#define KERN_PID 1

struct proctable {
    struct proc* proc_entries[PID_MAX + 1];
    unsigned next_pid;
    struct lock* lk_pt;
};

//initialize this before proc_bootstrap
void proctable_bootstrap(void);

// return error code, or 0 if successful
int proctable_assign_pid(struct proc *proc);

// special function to assign a PID to the first kernel process
void proctable_assign_kern_pid(struct proc *proc);

// removes a process from the process table, should be done before the process itself is destroyed
void proctable_unassign_pid(struct proc *proc);

struct proc *proctable_get_proc(pid_t pid);
#endif