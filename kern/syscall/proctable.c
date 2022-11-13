#include <proctable.h>

struct proctable *proctable;

void 
proctable_bootstrap(void)
{
    proctable = kmalloc(sizeof(struct proctable));
    if (proctable == NULL) {
        panic("Unable to allocate memory for process table\n");
    }

    for (int i = 0; i < PID_MAX + 1; i++) {
        proctable->proc_entries[i] = NULL;
    }

    proctable->next_pid = PID_MIN;
    proctable->lk_pt = lock_create("proctable lock");
    if (proctable->lk_pt == NULL) {
        panic("Unable to initialize lock for process table\n");
    }
}

int
proctable_assign_pid(struct proc *proc)
{
    lock_acquire(proctable->lk_pt);
    if (proctable->proc_entries[proctable->next_pid] == NULL) {
        proctable->proc_entries[proctable->next_pid] = proc;
        proc->pid = proctable->next_pid;
        proctable->next_pid++;
        lock_release(proctable->lk_pt);

        return 0;
    }

    unsigned start_pid = proctable->next_pid;
    proctable->next_pid++;

    while (proctable->next_pid != start_pid) {
        if (proctable->proc_entries[proctable->next_pid] == NULL) {
            proctable->proc_entries[proctable->next_pid] = proc;
            proc->pid = proctable->next_pid;
            proctable->next_pid++;
            lock_release(proctable->lk_pt);

            return 0;
        }

        proctable->next_pid++;
        if (proctable->next_pid > PID_MAX) {
            proctable->next_pid = PID_MIN;
        }
    }
    
    lock_release(proctable->lk_pt);
    return ENPROC;
}

void
proctable_assign_kern_pid(struct proc *proc)
{
    proctable->proc_entries[KERN_PID] = proc;
    proc->pid = KERN_PID;
}

void
proctable_unassign_pid(struct proc *proc)
{
    lock_acquire(proctable->lk_pt);
    proctable->proc_entries[proc->pid] = NULL;
    proc->pid = -1;
    lock_release(proctable->lk_pt);
}

struct proc *
proctable_get_proc(pid_t pid)
{
    return proctable->proc_entries[pid];
}
