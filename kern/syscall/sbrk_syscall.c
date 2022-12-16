#include <types.h>
#include <kern/errno.h>
#include <spinlock.h>
#include <lib.h>
#include <current.h>
#include <proc.h>
#include <addrspace.h>
#include <syscall.h>
#include <synch.h>
#include <vm.h>

extern unsigned nfreepages;
// extern struct lock *global_lock;
extern struct spinlock sbrk_lock;

int sys_sbrk(intptr_t amount, int32_t *retval)
{
    int err;
    *retval = -1;
    if (amount % PAGE_SIZE) {
        return EINVAL;
    }

    spinlock_acquire(&sbrk_lock);
    struct addrspace *as = proc_getas();
    vaddr_t heap_top = as->as_heap_top;
    vaddr_t new_top = heap_top + amount;
    
    if (new_top >= as->as_stack_top) {
        spinlock_release(&sbrk_lock);
        return EINVAL;
    }
    
    if (new_top < as->as_heap_base) {
        spinlock_release(&sbrk_lock);
        return EINVAL;
    }
    
    if (new_top > as->as_stack_base) {
        spinlock_release(&sbrk_lock);
        return ENOMEM;
    }
    
    if ((amount > 0) && ((new_top - as->as_heap_base) > MAX_HEAP)) {
        spinlock_release(&sbrk_lock);
        return ENOMEM;
    }
    
    if (amount == 0) {
        *retval = (int32_t) heap_top;
        spinlock_release(&sbrk_lock);
        return 0;
    }
    
    // the following vm functions are executed under a global lock
    if (amount > 0) {
        err = alloc_sbrk_pages(amount / PAGE_SIZE);
    }
    else {
        err = free_sbrk_pages(-amount / PAGE_SIZE);
    }
    
    if (err) {
        spinlock_release(&sbrk_lock);
        return err;
    }
    
    // kprintf("sbrk pages %d, heap %d, nfree %d\n", (int) amount, heap_top, nfreepages);
    // return the old heap top
    *retval = (int32_t) heap_top;
    spinlock_release(&sbrk_lock);
    return 0;
}