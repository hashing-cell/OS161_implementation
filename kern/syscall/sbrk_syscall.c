#include <types.h>
#include <kern/errno.h>
#include <spinlock.h>
#include <lib.h>
#include <current.h>
#include <proc.h>
#include <addrspace.h>
#include <syscall.h>
#include <vm.h>

extern unsigned nfreepages;

int sys_sbrk(intptr_t amount, int32_t *retval)
{
    int err;
    *retval = -1;
    if (amount % PAGE_SIZE) {
        return EINVAL;
    }

    struct addrspace *as = proc_getas();
    vaddr_t heap_top = as->as_heap_top;
    vaddr_t new_top = heap_top + amount;
    
    if (new_top >= as->as_stack_top) {
        return EINVAL;
    }
    
    if (new_top < as->as_heap_base) {
        return EINVAL;
    }
    
    if (new_top > as->as_stack_base) {
        return ENOMEM;
    }
    
    if ((amount > 0) && ((new_top - as->as_heap_base) > MAX_HEAP)) {
        return ENOMEM;
    }
    
    if (amount == 0) {
        *retval = (int32_t) heap_top;
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
        return err;
    }
    
    kprintf("sbrk pages %d, heap %d, nfree %d\n", (int) amount, heap_top, nfreepages);
    // return the old heap top
    *retval = (int32_t) heap_top;
    return 0;
}