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
#include <kern/wait.h>

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

    struct proc *child_proc = proctable_get_proc(pid);
    if (pid < PID_MIN || pid > PID_MAX || child_proc == NULL) {
        return ESRCH;
    }

    // Check if the PID of the process to be waited on is the parent of the current process
    if (child_proc->parent_pid != curproc->pid) {
        return ECHILD;
    }

    // We sleep and hold here if the child process is not finished
    lock_acquire(child_proc->wait_lock);
    int proc_state = child_proc->proc_state;
    while (proc_state != FINISHED) {
        cv_wait(child_proc->wait_signal, child_proc->wait_lock);
        proc_state = child_proc->proc_state;
    }
    lock_release(child_proc->wait_lock);

    // After collecting the child's exit code, we can allow it to terminate
    lock_acquire(child_proc->exit_lock);
    cv_broadcast(child_proc->exit_signal, child_proc->exit_lock);
    lock_release(child_proc->exit_lock);

    if (status != NULL) {
        int err = copyout(&child_proc->exit_code, status, sizeof(int));
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
    proc_exit(exitcode, __WEXITED);
    panic("Should not return");
    return 0;
}