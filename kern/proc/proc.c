/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <proctable.h>
#include <kern/wait.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/* Proc's Filetable*/
	proc->p_ft = filetable_create();
	if (proc->p_ft == NULL) {
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	/* Proc state */
	proc->pid = -1;
	proc->proc_state = INIT;
	proc->parent_pid = -1;
	proc->exit_code = -1;

	proc->children = array_create();
	if (proc->children == NULL) {
		filetable_destroy(proc->p_ft);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	proc->wait_lock = lock_create("waitpid lock");
	if (proc->wait_lock == NULL) {
		array_destroy(proc->children);
		filetable_destroy(proc->p_ft);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	
	proc->wait_signal = cv_create("waitpid cv");
	if (proc->wait_signal == NULL) {
		lock_destroy(proc->wait_lock);
		array_destroy(proc->children);
		filetable_destroy(proc->p_ft);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	

	proc->exit_lock = lock_create("exit lock");
	if (proc->exit_lock == NULL) {
		cv_destroy(proc->wait_signal);
		lock_destroy(proc->wait_lock);
		array_destroy(proc->children);
		filetable_destroy(proc->p_ft);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	
	proc->exit_signal = cv_create("exit cv");
	if (proc->exit_signal == NULL) {
		lock_destroy(proc->exit_lock);
		cv_destroy(proc->wait_signal);
		lock_destroy(proc->wait_lock);
		array_destroy(proc->children);
		filetable_destroy(proc->p_ft);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	proctable_unassign_pid(proc);
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	int num_threadarray = threadarray_num(&proc->p_threads);
	for (int i = 0; i < num_threadarray; i++){
		threadarray_remove(&proc->p_threads, 0);
	}

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	int num_procs = array_num(proc->children);
	for (int i = 0; i < num_procs; i++){
		array_remove(proc->children, 0);
	}

	if (proc->children) {
		array_destroy(proc->children);
		proc->children = NULL;
	}

	if (proc->exit_lock) {
		lock_destroy(proc->exit_lock);
		proc->exit_lock = NULL;
	}

	if (proc->exit_signal) {
		cv_destroy(proc->exit_signal);
		proc->exit_signal = NULL;
	}

	if (proc->wait_lock) {
		lock_destroy(proc->wait_lock);
		proc->wait_lock = NULL;
	}

	if (proc->wait_signal) {
		cv_destroy(proc->wait_signal);
		proc->wait_signal = NULL;
	}

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}

	proctable_assign_kern_pid(kproc);
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}
	
	if (proctable_assign_pid(newproc)) {
		proc_destroy(newproc);
		return NULL;
	}

	/* Process state */
	newproc->proc_state = NORMAL; //Since this is the first process, we there is no parent
	newproc->parent_pid = KERN_PID;

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/* Process Filetable */
	init_stdio(newproc->p_ft);
	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Create a proc for use by sys_fork.
 *
 */
int
proc_create_sysfork(struct proc **p_new_forked_proc)
{
	*p_new_forked_proc = proc_create("Forked process");
	if (*p_new_forked_proc == NULL) {
		return ENOMEM;
	}
	
	if (proctable_assign_pid(*p_new_forked_proc)) {
		proc_destroy(*p_new_forked_proc);
		return ENPROC;
	}

	/* Process state */
	(*p_new_forked_proc)->proc_state = NORMAL;
	(*p_new_forked_proc)->parent_pid = curproc->pid;

	if (array_add(curproc->children, *p_new_forked_proc, NULL)) {
		return ENOMEM;
	}

	/* VM fields */
	struct addrspace *as = proc_getas();
	if (as != NULL) {
        spinlock_acquire(&as->as_lock);
        (*p_new_forked_proc)->p_addrspace = as;
        as->as_refcount++;
        spinlock_release(&as->as_lock);
	}

	/* VFS fields */

	/* Process Filetable */
	filetable_dup(curproc->p_ft, (*p_new_forked_proc)->p_ft);
	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		(*p_new_forked_proc)->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return 0;
}


/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}


void
proc_exit(int exit_code, int w_origin) {
	switch (w_origin) {
		case __WEXITED:
			curproc->exit_code = _MKWAIT_EXIT(exit_code);
			break;
		case __WSIGNALED:
			curproc->exit_code = _MKWAIT_SIG(exit_code);
			break;
		case __WCORED:
			curproc->exit_code = _MKWAIT_CORE(exit_code);
			break;
		default:
			curproc->exit_code = _MKWAIT_STOP(exit_code);
			break;
	}

	// We check their children
	// - we wake up all finished children (finished children will destroy themselves after wake)
	// - otherwise we will orphan children that are running
	int num_child_procs = array_num(curproc->children);
	for (int i = 0; i < num_child_procs; i++) {
		struct proc *child_proc = array_get(curproc->children, i);
		lock_acquire(child_proc->exit_lock);
		if (child_proc->proc_state == FINISHED) {
			cv_broadcast(child_proc->exit_signal, child_proc->exit_lock);
		} else if (child_proc->proc_state == NORMAL) {
			child_proc->proc_state = ORPHAN;
		}
		lock_release(child_proc->exit_lock);
	}


	// If we still have a parent, we go to the finished state and sleep until the process' parent signals to wake up, where it will then destroy itself
	// Otherwise we destroy this process immediately
	if (curproc->proc_state == NORMAL) {
		curproc->proc_state = FINISHED;

		// Broadcast its completion
		lock_acquire(curproc->wait_lock);
		cv_broadcast(curproc->wait_signal, curproc->wait_lock);
		lock_release(curproc->wait_lock);

		// Go to sleep until its parent is finished or its parent is destroyed
		lock_acquire(curproc->exit_lock);
		cv_wait(curproc->exit_signal, curproc->exit_lock);
		lock_release(curproc->exit_lock);
	}

	thread_proc_exit();
}
