/*
    Our system call implementations here

    recommended same person do fork, waipid, and _exit according to prof
*/
#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <mips/trapframe.h>


int
sys_getpid(pid_t *retval)
{
    (void) retval;
    //stuff
    return 0;
}

int
sys_fork(struct trapframe *tf, pid_t *retval)
{
    (void) tf; (void) retval;
    //stuff
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
    (void) pid; (void) status; (void) options; (void) retval;
    //stuff
    return 0;
}

int
sys__exit(int exitcode)
{
    (void) exitcode;
    //stuff
    return 0;
}