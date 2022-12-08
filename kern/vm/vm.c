#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <current.h>
#include <machine/vm.h>
#include <spinlock.h>
#include <synch.h>
#include <mips/tlb.h>
#include <spl.h>
#include <proc.h>
#include <addrspace.h>
#include <vm.h>
#include <cpu.h>
#include <signal.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <vnode.h>
//#include <swap.h>


//extern struct swapfile swfile;

// declare a global coremap
struct coremap coremap;

struct lock *global_lock;
struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
struct spinlock tlb_lock = SPINLOCK_INITIALIZER;

// range of entry indices controlled by VM
unsigned first_page, last_page;
unsigned nfreepages;
unsigned next_free;     // next location for page allocation
unsigned swapclock;     // next victim for page replacement

bool vm_initialized = false;

/*
 *  vm_bootstrap - Initialize vm
 *
 */
void
vm_bootstrap ()
{
    unsigned i;
    paddr_t addr;
    
    spinlock_init(&coremap_lock);
    spinlock_init(&tlb_lock);
    global_lock = lock_create("global_lock");
    
//    cm_lock = lock_create("virtual memory");

    // cannot open the swap file during startup
    // open later when needed
//    swfile.sw_vnode = NULL;

    vm_initialized = true;

    // compute the range of pages to be controlled by VM
    addr = ram_getsize();
    last_page = (addr >> PAGE_OFFSET_BITS);
    
    addr = ram_getfirstfree();
    first_page = (addr >> PAGE_OFFSET_BITS);
    next_free = swapclock = first_page;
    nfreepages = last_page - first_page;
    
//    swfile.sw_first_page = swfile.sw_next_free = 0;
//    swfile.sw_last_page = swfile.sw_nfreepages = NUM_SW_PAGES;
    
    // initialize the coremap
    for (i=first_page; i<NUM_PPAGES; i++) {
//        coremap.cm_ppmap[i] =  (i << PAGE_OFFSET_BITS);
        coremap.cm_ppmap[i] = PP_FREE;
    }
    
    // pages before first_page are fixed, never be evicted
    for (i=0; i<first_page; i++) {
        coremap.cm_ppmap[i] |= PP_FIXED;
    }
    
    for (i=0; i<NUM_SW_PAGES; i++) {
        // (i << PAGE_OFFSET_BITS) is the file location
        coremap.cm_swapmap[i] = 0;
    }
}

#define IS_PPAGE_USE(ppentry)       (ppentry & PP_USE)
#define CLEAR_PPAGE_USE(ppentry)    (*ppentry & ~PP_USE)

/*
 *  vm_tlbshootdown - Remove an entry from another CPU’s TLB address mapping
 *
 */
void
vm_tlbshootdown (const struct tlbshootdown *tlb)
{
    (void) tlb;
}

void
vm_tlbshootdown_all(void)
{

}

void
vm_tlbinvalidate(void)
{
    int i, spl;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

static
paddr_t
acquire_pages (unsigned npages)
{
    unsigned nfree = 0;
    unsigned start = next_free;
    unsigned i, j;
    ppagemap_t entry;
    paddr_t ppage_addr = 0;
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }
    
//    lock_acquire(cm_lock);
    
    for (i=first_page; i<last_page; i++) {
        entry = coremap.cm_ppmap[start];
        if (IS_PPAGE_FREE(entry)) {
            nfree++;
        }
        else {
            nfree = 0;
        }
        start++;
        if (start >= last_page) {
            // we don't support wrap around:
            // continous pages don't wrap around 
            start = first_page;
            nfree = 0;
        }

        if (nfree == npages) {
            // found
            // update next_free
            next_free = start;
            pid_t pid = curproc->pid;
            pid <<= 6;

            // mark all these pages as used
            for (j=0; j<npages; j++) {
                start--;
                if (j==0) {
                    coremap.cm_ppmap[start] = (PP_ALLOC_END | PP_DIRTY | PP_USE | pid);
                    nfreepages--;
                }
                else {
                    coremap.cm_ppmap[start] = (PP_DIRTY | PP_USE | pid);
                    nfreepages--;
                }
//                kprintf("alloc_kpages %x\n", coremap.cm_ppmap[start]);
            }
            
            ppage_addr = (paddr_t) (start << PAGE_OFFSET_BITS);
            break;
        }
    }
    
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }
//    lock_release(cm_lock);
    
//    kprintf("alloc_kpages %d, %x\n", npages, ret);
    return ppage_addr;
}

/*
 *  alloc_kpages - allocate n contiguous physical pages
 *  return vaddr_t
 *
 */
vaddr_t
alloc_kpages (unsigned npages)
{
    paddr_t addr;
//    pid_t pid = 0;
    
    if (!vm_initialized) {
        addr = ram_stealmem(npages);
    }
    else {
        addr = acquire_pages(npages);
        
        struct addrspace *as = proc_getas();
        if (as != NULL) {
            as->as_kpages += npages;
        }
//        KASSERT(pid > 0);
    }
    
    if (addr > 0) {
        return PADDR_TO_KVADDR(addr);
    }
    return 0;
}

/*
 *  free_kpages - free pages starting at given physical addr
 *
 */
void
free_kpages (vaddr_t addr)
{
    unsigned entry_idx = (KVADDR_TO_PADDR(addr) & PAGE_FRAME) >> PAGE_OFFSET_BITS;
    // KASSERT(entry_idx >= first_page && entry_idx < last_page);
    KASSERT(entry_idx < last_page);
    
//    lock_acquire(cm_lock);
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }

    bool done = false;
    do {    
        done = IS_PPAGE_ALLOC_END(coremap.cm_ppmap[entry_idx]);
        coremap.cm_ppmap[entry_idx] = PP_FREE;
        entry_idx++;
        nfreepages++;
    }
    while (!done && (entry_idx < last_page));
   
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }
    
//    lock_release(cm_lock);

}

/*
 *  vm_fault - handle vm faults
 *
 */
int
vm_fault (int faulttype, vaddr_t faultaddress)
{
    struct addrspace *as;
    
    switch (faulttype) {
        case VM_FAULT_READONLY:
            KASSERT(faulttype == VM_FAULT_READONLY);
            break;

        case VM_FAULT_READ:
            break;
            
        case VM_FAULT_WRITE:
            break;
            
        default:
            return EINVAL;
    }
    
   	if (curproc == NULL) {
		//
		//  No process. This is probably a kernel fault early
		// in boot. Return EFAULT so as to panic instead of
		//  getting into an infinite faulting loop.
		//
		return EFAULT;
	}

   	as = proc_getas();
	if (as == NULL) {
		//
		// No address space set up. This is probably also a
		// kernel fault early in boot.
		//
		return EFAULT;
	}
	
	// check if the faultaddress within userspace
	if (!as_is_valid_address(as, faultaddress)) {
	    return SIGSEGV;
	}
	
    if (nfreepages <= MIN_FREE_PAGES) {	
//        do_page_replacement();
    }

    faultaddress &= PAGE_FRAME;
//    lock_acquire(global_lock);    

    int err;
    pagetable_t pt_entry;
    
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }

    err = as_get_pt_entry(as, faultaddress, &pt_entry);
    if (err) {
        return err;
    }
    
    if (pt_entry == 0) {
        
        paddr_t ppage = acquire_pages(1);
        if (ppage == 0) {
            return ENOMEM;
        }
        
        as->as_vpages++;
        // update pagetable entry with the page address in RAM
        
        pt_entry = ((ppage & PAGE_FRAME) | PT_PRESENT_MASK | PT_DIRTY_MASK);
        as_set_pt_entry(as, faultaddress, pt_entry);
        
        unsigned cmidx = ((ppage & PAGE_FRAME) >> PAGE_OFFSET_BITS);
        coremap.cm_ppmap[cmidx] |= (faultaddress);
        

        // for testing        
        //pagetable_t pt_entry2;
        //as_get_pt_entry(as, faultaddress, &pt_entry2);
        //KASSERT(pt_entry == pt_entry2);
    }
    else {
        pt_entry = (pt_entry & PAGE_FRAME) | PT_PRESENT_MASK | PT_USED_MASK;
        as_set_pt_entry(as, faultaddress, pt_entry);
    }
    
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }

    uint32_t entryhi, entrylo;
    pid_t pid = curproc->pid;
    
    entryhi = faultaddress | pid << 6;
    
    bool writeable = true;
    if (faultaddress >= as->as_code_base && faultaddress < as->as_code_top) {
        writeable = (as->as_code_permission & AS_WRITEABLE);
    }
    else if (faultaddress >= as->as_data_base && faultaddress < as->as_data_top) {
        writeable = (as->as_data_permission & AS_WRITEABLE);
    }
    
    if (faulttype == VM_FAULT_WRITE) {
        if (writeable) {
            entrylo = (pt_entry & PAGE_FRAME) | TLBLO_VALID | TLBLO_DIRTY;
        }
        else {
            return EPERM;
        }
    }
    else {
        if (writeable) {
            entrylo = (pt_entry & PAGE_FRAME) | TLBLO_VALID | TLBLO_DIRTY;
        }
        else {
            entrylo = (pt_entry & PAGE_FRAME) | TLBLO_VALID;
        }
    }
    
//   	struct _dbg dbg;
//    dbg.ehi = (vaddr_t) entryhi;
//    dbg.elo = (vaddr_t) entrylo;
//    dbg.faultpage = (vaddr_t) faultaddress;
//    dbg.page_entry = (vaddr_t) pt_entry;
//    dbg.occupied = occupied;
    
//    KASSERT(dbg.faultpage);
        
 
      spinlock_acquire(&tlb_lock);
    
/*    
if (faultaddress == 0x400000) {
        uint32_t tlbHi, tlbLo;
        if (get_tlb_value(faultaddress, &tlbHi, &tlbLo)) {
            KASSERT(faultaddress == 0x400000);
        }
    } 
*/
    
    int spl = splhigh();
    uint32_t idx;
    uint32_t ehi, elo;
    for (idx = 0; idx < NUM_TLB; idx++) {
        tlb_read(&ehi, &elo, idx);
        
        if ((entryhi & TLBHI_VPAGE) == (ehi & TLBHI_VPAGE)) {
            tlb_write(entryhi, entrylo, idx);
            break;
        }
    }
    
    if (idx == NUM_TLB) {
        // entry not in TLB, write to a randon location
        tlb_random(entryhi, entrylo);
    }

    splx(spl);

/*    
    if (faultaddress == 0x400000) {
        uint32_t tlbHi, tlbLo;
        if (get_tlb_value(faultaddress, &tlbHi, &tlbLo)) {
            KASSERT(faultaddress == 0x400000);
        }
    }
*/    
    
//    if (!acquired) {
        spinlock_release(&tlb_lock);
    //}
  //  lock_release(global_lock);
    return 0;
}

int 
alloc_sbrk_pages(unsigned npages)
{
    lock_acquire(global_lock);
    struct addrspace *as = proc_getas();
    vaddr_t vaddr;
    unsigned i;
    vaddr_t heap_top = as->as_heap_top;
    pagetable_t pte;
    ppagemap_t ppe;
    paddr_t cmidx;
    pid_t pid = curproc->pid << 6;
    bool acquired = spinlock_do_i_hold(&coremap_lock);

    if (!acquired) {    
        spinlock_acquire(&coremap_lock);
    }
    if (nfreepages > npages) {
        for (i=0; i<npages; i++) {
            vaddr = acquire_pages(1);
            pte = ((vaddr & PAGE_FRAME) | PT_PRESENT_MASK | PT_DIRTY_MASK);
            cmidx = (vaddr & PAGE_FRAME) >> PAGE_OFFSET_BITS;

            // set the coremap entry
            ppe = ((heap_top & PAGE_FRAME) | PP_ALLOC_END | PP_DIRTY | PP_USE | pid);
            coremap.cm_ppmap[cmidx] = ppe;            
    
            // set the pagetable entry, create pagetable when needed
            as_set_pt_entry(as, heap_top, pte);
            
//            kprintf("vaddr %x, paddr %x\n", heap_top, vaddr); 
            
            heap_top += PAGE_SIZE;
        }
    }
    
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }
        
    as->as_heap_top = heap_top;
    
    lock_release(global_lock);
    return 0;
}

int 
free_sbrk_pages(unsigned npages)
{
    unsigned i;
    struct addrspace *as = proc_getas();
    vaddr_t heap_top;
    pagetable_t pte;
    ppagemap_t ppe;
    paddr_t cmidx;
    
    lock_acquire(global_lock);
    
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {    
        spinlock_acquire(&coremap_lock);
    }
    
    if (npages > 0) {
        heap_top = as->as_heap_top - (npages * PAGE_SIZE);
        as->as_heap_top = heap_top;
        
        for (i=0; i<npages; i++) {
            as_get_pt_entry(as, heap_top, &pte);
            cmidx = (pte & PAGE_FRAME) >> PAGE_OFFSET_BITS;

            // set the coremap entry
            ppe = PP_FREE;
            coremap.cm_ppmap[cmidx] = ppe;            
    
            // set the pagetable entry
            as_set_pt_entry(as, heap_top, 0);
            
            nfreepages++;
            heap_top += PAGE_SIZE;
        }
    }
    if (!acquired) {    
        spinlock_release(&coremap_lock);
    }

    
    lock_release(global_lock);
    
    return 0;
}

