/*
    Our system call implementations here

    recommended same person do fork, waipid, and _exit according to prof
*/
#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <thread.h>
#include <proctable.h>
#include <copyinout.h>

int
sys_getpid(pid_t *retval)
{
    *retval = curproc->pid;
    return 0;
}

static
void
begin_forked_process(void *p, unsigned long arg)
{
    (void) arg;

    struct trapframe tf;
    memcpy(&tf, p, sizeof(struct trapframe));
    tf.tf_v0 = 0;
    tf.tf_a3 = 0;
    tf.tf_epc += 4;
    kfree(p);
    as_activate();
    
    mips_usermode(&tf);
}

int
sys_fork(struct trapframe *tf, pid_t *retval)
{
    int err;
    *retval = -1;
    struct proc *child_proc;

    err = proc_create_sysfork(&child_proc);
    if (err) {
        return err;
    }

    struct addrspace *parent_as = proc_getas();
    err = as_copy(parent_as, &child_proc->p_addrspace);
	if (err) {
		proc_destroy(child_proc);
		return err;
	}

    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
        proc_destroy(child_proc);
        return ENOMEM;
    }
    memcpy(child_tf, tf, sizeof(struct trapframe));

    err = thread_fork("child process", child_proc, begin_forked_process, (void *) child_tf, 0);
    if (err) {
        proc_destroy(child_proc);
        kfree(child_tf);
        return err;
    }

    *retval = child_proc->pid;
    return 0;
}

int
sys_execv(const userptr_t program, userptr_t args)
{
    (void) program; (void) args;
    //stuff
    return 0;
}

int
sys_waitpid(pid_t pid, userptr_t status, int options, pid_t* retval)
{
    if (options != 0) {
        return EINVAL;
    }

    struct proc *wait_proc = proctable_get_proc(pid);
    if (pid < PID_MIN || pid > PID_MAX || wait_proc == NULL) {
        return ESRCH;
    }

    // Check if the PID of the process to be waited on is the parent of the current process
    if (wait_proc->parent_pid != curproc->pid) {
        return ECHILD;
    }

    if (wait_proc->proc_state != FINISHED) {
        cv_wait(wait_proc->wait_signal, wait_proc->wait_lock);
    }

    if (status != NULL) {
        int err = copyout(&wait_proc->exit_code, status, sizeof(int));
        if (err) {
            return err;
        }
    }

    *retval = pid;
    return 0;
}

int
sys__exit(int exitcode)
{
    (void) exitcode;
    //stuff
    return 0;
}