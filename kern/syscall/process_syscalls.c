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
#include <vfs.h>
#include <limits.h>

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

static
int get_user_strlen(char* str, size_t* len) {
    size_t count = 0;
    const char* curr_char = (const char*) str;
    *len = 0;
    size_t maxlen = 0;

    if ((vaddr_t) str >= USERSPACETOP) {
		/* region is within the kernel */
		return EFAULT;
	}

    maxlen = USERSPACETOP - (vaddr_t) str;

    while(*curr_char) {
        if((vaddr_t) curr_char < (vaddr_t) str) {
            return EFAULT;
        }
        if (count >= maxlen) {
            /* region within the kernel */
            return EFAULT;
        }
        count++;
        curr_char++;
    }
    *len = count;
    return 0;
}

static
int copy2kernel(char **argv, size_t *argc, char **kbuffer, size_t *bufsize) {
    size_t kbufsize = 0;
    size_t idx = 0;  //curr 
    char* kbuf;
    size_t len = 0;
    int err;
    
    //calc kbufsize
    while(argv[idx] != NULL) {
        err = get_user_strlen(argv[idx], &len);
        if(err) {
            return -1;
        }
        kbufsize += len + (4 - (len % 4));

        if(kbufsize >= ARG_MAX) {
            return E2BIG;
        }
        idx++;
    }

    kbufsize += (idx+1) * (size_t)sizeof(char*); //for storing offset of karg and null before arg values
    kbuf = kmalloc(kbufsize);

    bzero(kbuf, kbufsize);

    size_t offset = (idx+1) * (size_t)sizeof(char*);
    size_t arg_len = 0;
    size_t arg_padded_len = 0;
    idx = 0;
    unsigned int* kargv = (unsigned int*) kbuf;
    while(argv[idx]) {
        arg_len = strlen(argv[idx]);
        arg_padded_len = arg_len+(4 - (arg_len % 4));
        copyin((const_userptr_t)argv[idx], kbuf+offset, arg_padded_len);
        
        //fill kargv[idx] addresses
        *kargv = offset;
        kargv++;
       
        idx++;
        offset += arg_padded_len;
    }
    *bufsize = kbufsize;
    *kbuffer = kbuf;
    *argc = idx;
    return 0;

}

int
sys_execv(const char* program, char** args, pid_t* retval)
{
    //stuff
    size_t argc;
    char* kbuf;
    size_t kbufsize;
    size_t prog_gotlen;
    int err;
    *retval = -1;

    if (program == NULL) {
        return EFAULT;
    }
    
    if (args == NULL) {
        return EFAULT;
    }

    char* progname = kmalloc(PATH_MAX);
    if (progname == NULL) {
        return ENOMEM;
    }
    //kprintf("sizeof: %d, strlen: %d", sizeof(program), strlen(program));
    err = copyinstr((const_userptr_t)program, progname, PATH_MAX, &prog_gotlen);
    if (err) {
        kfree(progname);
        return err;
    }

    if (prog_gotlen == 0 || progname[0] == '\0') {
        kfree(progname);
        return EINVAL;
    }

    //copy args from userspace to kernel
    err = copy2kernel(args, &argc, &kbuf, &kbufsize);
    if (err) {
        kfree(progname);
        return err;
    }
    
    struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	as_destroy(proc_getas());

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

    stackptr -= kbufsize;

    /* Set kargv[i] to correct addr in stack. Starting w kargv[0] = stackptr */
    unsigned int* kbuf_idx = (unsigned int *)kbuf;
    for(unsigned int i = 0; i < argc; i++) {
        *kbuf_idx += stackptr;
        kbuf_idx++;
    }

    /* Copy from kernel to new user stack*/
    copyout(kbuf, (userptr_t) stackptr, kbufsize);
    kfree(kbuf);
    kfree(progname);
    
    /* Warp to user mode. */
	enter_new_process((int)argc, (userptr_t) stackptr, NULL, stackptr, entrypoint);
    /* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
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