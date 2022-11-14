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
int get_user_strlen(const char *str, int *len) 
{
    char buf[64];
    size_t maxlen = 64;
    size_t read;
    int err;
    const char *p_instr = str;
    int strlen = 0;

    do {    
        err = copyinstr((const_userptr_t) p_instr, buf, maxlen, &read);
        if (err == ENAMETOOLONG) 
            read = maxlen;
        else if (err) {
            return err;
        }

        p_instr += read;
        strlen += read;
    } 
    while ( (err == ENAMETOOLONG) || buf[read - 1] !='\0');
    
    *len = strlen;  // strlen includes the NULL at the end
    return 0;
}

static
int get_user_argv_addr(const_userptr_t argv, vaddr_t *addr) {
    char buf[8];  //only copying in 4 but have 8 just in case of overflow
    int err;
    
    if (argv == NULL) {
        return EFAULT;
    }
    
    err = copyin(argv, buf, 4);
    if (err) {
        return err;
    }
    
    *addr = *((vaddr_t *) buf);
    return 0;
}

static
int compute_argument_buffer(char **argv, size_t *argc, size_t *bufsize)
{
    char *p_argv = (char *) argv;
    char *p_arg;
    vaddr_t arg_addr;
    int err, strlen;
    int bufsz = 0;
    int arg_count = 0;
    
    *argc = 0;
    *bufsize = 0;
    
    do {
        err = get_user_argv_addr((const_userptr_t) p_argv, &arg_addr);
        if (err) {
            return err;
        }
        
        if (arg_addr == 0) {
            break;
        }
        
        p_arg = (char *) arg_addr;
        p_argv += 4;
        
        err = get_user_strlen(p_arg, &strlen);
        if (err) {
            return err;
        }
        
//        kprintf("strlen %d\n", strlen);
        
        bufsz += strlen;    //strlen already includes the NULL terminator
        if (strlen % 4) {
            bufsz += (4 - (strlen % 4));  //calc padding for alignment
        }

        if (bufsz >= ARG_MAX) {
            return E2BIG;
        }
            
        arg_count++;
     }
     while (1);
     
    //add buffer for (arg_count + 1) pointers
    //extra pointer is the NULL pointer at the end of argv
    bufsz += (int) (sizeof(char *) * (arg_count+1));

    *argc = arg_count;
    *bufsize = bufsz;
    return 0;
}

// copy the input args from userspace to kernel buffer
static
int copyin_arguments(char **in_argv, int argc, int bufsize, char *outbuf)
{
    char *p_inargv = (char *) in_argv;
    char *p_outbuf = outbuf;
    char *p_inargument;
    char *p_outargument;
    vaddr_t arg_addr;
    int err, arg_len, total_arg_len;
    int arg_offset;
    
    // fill outbuf with zeros
    bzero(outbuf, bufsize);
        
    // p_outbuf points to the base address of outbuf, which stores the address of arguments
    p_outbuf = outbuf;
    
    // arg_start is the offset from the outbuf
    arg_offset = (argc + 1) * sizeof(vaddr_t);
    
    // p_outargument points to the first argument address within the output buffer
    p_outargument = outbuf + arg_offset;  
    
    // accumulated argument length
    total_arg_len = 0;
    
    
    for (int i=0; i<argc; i++) {
        err = get_user_argv_addr((const_userptr_t) p_inargv, &arg_addr);
        if (err) {
            return err;
        }
        
        // advance to the next argument address
        p_inargv += sizeof(vaddr_t);    // = 4
        
        // point to the input argument string
        p_inargument = (char *) arg_addr;
        err = get_user_strlen(p_inargument, &arg_len);
        if (err) {
            return err;
        }
        
        // kprintf("p_inargument %s\n", p_inargument);
        // write the argument
        // arg_len already includes the NULL terminator so copyin and copyinstr are the same
        err = copyin((const_userptr_t) p_inargument, p_outargument, arg_len);
        if (err) {
            return err;
        }
        
        // write the argument address
        *((vaddr_t *) p_outbuf) = (vaddr_t) (arg_offset + total_arg_len);
        
        // advance to location for next argument address
        p_outbuf += sizeof(vaddr_t);    // sizeof(vaddr_t) = 4

        // add padding
        if (arg_len % 4) {
            arg_len += (4 - arg_len % 4);
        }
        
        // update accumulated argument length
        total_arg_len += arg_len;
//        kprintf("total_arg_len %d\n", total_arg_len);
        
        // advance to next output argument location, account for padding
        p_outargument += arg_len;
    }
    
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
    err = compute_argument_buffer(args, &argc, &kbufsize);
    if (err) {
        kfree(progname);
        return err;
    }
    
    kbuf = kmalloc(kbufsize);
    if(kbuf == NULL) {
        kfree(progname);
        return ENOMEM;
    }

    err = copyin_arguments(args, argc, kbufsize, kbuf);
    if (err) {
        kfree(progname);
        kfree(kbuf);
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

    /* Set kargv[i] to correct addr in stack. Starting w kargv[0] = stackptr+offset of 1st arg */
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