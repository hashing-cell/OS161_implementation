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


// declare a global coremap
struct coremap coremap;
uint32_t user_base_addr;
uint32_t* _coremap;
struct swapentries * _swapmap;

struct lock *global_lock;
struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
struct spinlock tlb_lock = SPINLOCK_INITIALIZER;
struct spinlock sbrk_lock = SPINLOCK_INITIALIZER;

// range of entry indices controlled by VM
unsigned last_page;
unsigned nfreepages;
unsigned next_free;     // next location for page allocation

struct vnode *swap_vnode;
struct spinlock swapmap_lock = SPINLOCK_INITIALIZER;
char swap_file[] = "lhd0raw:";
unsigned swapclock;     // next victim for page replacement
unsigned swap_base;
unsigned swap_last_page; 

bool vm_initialized = false;

/*
 *  vm_bootstrap - Initialize vm
 *
 */
void
vm_bootstrap ()
{
    uint32_t i;
    
    spinlock_init(&coremap_lock);
    spinlock_init(&tlb_lock);
    global_lock = lock_create("global_lock");
    spinlock_init(&sbrk_lock);
    spinlock_init(&swapmap_lock);

    // compute the range of pages to be controlled by VM
    uint32_t ramsize = ram_getsize();
    uint32_t num_entries = (ramsize - ram_stealmem(0)) / PAGE_SIZE;    
    _coremap = kmalloc(num_entries * sizeof(uint32_t));

    user_base_addr = ram_stealmem(0);
    last_page = (ramsize - user_base_addr) / PAGE_SIZE;

    next_free = swapclock = 0;
    nfreepages = last_page;
    vm_initialized = true;
    for (i=0; i<last_page; i++) {
        _coremap[i] = PP_FREE;    
    }


    //Initialize swap stuff
    if (vfs_open(swap_file, O_RDWR, 0, &swap_vnode)) {
        panic("Error openning swapfile\n");
    }
    swap_base = last_page;
    _swapmap = kmalloc(NUM_SW_PAGES * sizeof(struct swapentries));
    for (unsigned i = 0; i < NUM_SW_PAGES; i++) {
        _swapmap[i].in_use = false;
    }
}
/*_
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
    uint32_t entry;
    paddr_t ppage_addr = 0;
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }
    
//    lock_acquire(cm_lock);
    
    for (i=0; i<last_page; i++) {
        entry = _coremap[start];
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
            start = 0;
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
                    _coremap[start] = (PP_ALLOC_END | PP_DIRTY | PP_USE | pid);
                    nfreepages--;
                }
                else {
                    _coremap[start] = (PP_DIRTY | PP_USE | pid);
                    nfreepages--;
                }
            }
            
            ppage_addr = (paddr_t) (user_base_addr + (start * PAGE_SIZE));
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

static
vaddr_t
acquire_one_page (void)
{
    unsigned start = next_free;
    unsigned i;
    uint32_t cme;
    pid_t pid = curproc->pid;
    pid <<= 6;

    bool acquired = spinlock_do_i_hold(&coremap_lock);
    
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }
    
    for (i=0; i<last_page; i++) {
        cme = _coremap[start];
        if (IS_PPAGE_FREE(cme)) {
            _coremap[start] = (PP_ALLOC_END | PP_DIRTY | PP_USE | pid);
            nfreepages--;
            
            next_free = start + 1;
            if (next_free >= last_page)
                next_free = 0;
                
            break;
        }

        start++;
        if (start >= last_page)
            start = 0;
    }
    
    if (start == last_page) {
        // none free
        start = 0;
    }
    
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }

    return (user_base_addr + (start * PAGE_SIZE));
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
        addr = (npages == 1) ? acquire_one_page() : acquire_pages(npages);
        
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
//    unsigned entry_idx = (KVADDR_TO_PADDR(addr) & PAGE_FRAME) >> PAGE_OFFSET_BITS;
//    KASSERT(entry_idx < last_page);
    int32_t idx = ((KVADDR_TO_PADDR(addr) & PAGE_FRAME) - user_base_addr) / PAGE_SIZE;
   if (idx < 0 || idx >= (int) last_page) {
    //    kprintf("free_kpages %d\n", idx);
       return;
   }
    // KASSERT(idx >= 0 && idx < (int32_t) last_page);
    uint32_t entry_idx = idx;
    
//    lock_acquire(cm_lock);
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }

    bool done = false;
    do {    
        done = IS_PPAGE_ALLOC_END(_coremap[entry_idx]);
        _coremap[entry_idx] = PP_FREE;
        entry_idx++;
        nfreepages++;
    }
    while (!done && (entry_idx < last_page));
   
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }
    
//    lock_release(cm_lock);

}

int
duplicate_pagetable(struct pagetable* from, struct pagetable *to)
{
    KASSERT(from != NULL);
    KASSERT(to != NULL);
//    bool acquired = spinlock_do_i_hold(&coremap_lock);
//    if (!acquired)
//        spinlock_acquire(&coremap_lock);

    paddr_t paddr_from;
    paddr_t paddr_to;
    paddr_t cmidx_from;
    paddr_t cmidx_to;
    
    unsigned i;
    for (i=0; i<NUM_PTE; i++) {
        if (from->pt_entries[i] != 0) {
            paddr_from = (from->pt_entries[i] & PAGE_FRAME);
            cmidx_from = (from->pt_entries[i] - user_base_addr) / PAGE_SIZE;
            if (cmidx_from >= last_page) {
                continue;
            }
            
            paddr_to = acquire_one_page();
            paddr_to &= PAGE_FRAME;
            if (paddr_to == 0) {
                // no memory
                kprintf("no memery\n");
                return ENOMEM;
            }

            // copy the page content to the new page
            memcpy((void*) KVADDR_TO_PADDR(paddr_to), (const void*) KVADDR_TO_PADDR(paddr_from), PAGE_SIZE);
            
            to->pt_entries[i] = (paddr_to | (from->pt_entries[i] & 0xFFF));
            cmidx_to = (paddr_to - user_base_addr) / PAGE_SIZE;
            _coremap[cmidx_to] = _coremap[cmidx_from];
        }
    }
//    if (!acquired)
//        spinlock_release(&coremap_lock);
    return 0;
}

/*
 *  vm_fault - handle vm faults
 *
 */
int
vm_fault (int faulttype, vaddr_t faultaddress)
{
    struct addrspace *as;
    pid_t pid = curproc->pid;
    
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

    lock_acquire(global_lock);
   	as = proc_getas();
	if (as == NULL) {
		//
		// No address space set up. This is probably also a
		// kernel fault early in boot.
		//
        lock_release(global_lock);
		return EFAULT;
	}
	
	// check if the faultaddress within userspace
	if (!as_is_valid_address(as, faultaddress)) {
        lock_release(global_lock);	
	    return SIGSEGV;
	}
	
    if (nfreepages <= MIN_FREE_PAGES) {	
        swapout();
    }

    faultaddress &= PAGE_FRAME;  

    int err;
    pagetable_t pt_entry;
    
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {
        spinlock_acquire(&coremap_lock);
    }

    bool as_acquired = spinlock_do_i_hold(&as->as_lock);
    if (!as_acquired) {
        spinlock_acquire(&as->as_lock);
    }

    err = as_get_pt_entry(as, faultaddress, &pt_entry);
    if (err) {
        if (!as_acquired) {
            spinlock_release(&as->as_lock);
        }
        
        if (!acquired) {
            spinlock_release(&coremap_lock);
        }
   		lock_release(global_lock);
    
        return err;
    }
    
    err = swapin(faultaddress, pid);
    if (err == SWAPIN_NO_MEM) {
        panic("We did a swap out earlier...");
    }

    if (pt_entry == 0) {
        
        paddr_t ppage = acquire_one_page();
        if (ppage == 0) {
            if (!as_acquired) {
                spinlock_release(&as->as_lock);
            }
        
            if (!acquired) {
                spinlock_release(&coremap_lock);
            }
            
       		lock_release(global_lock);
            return ENOMEM;
        }
        
        as->as_vpages++;
        // update pagetable entry with the page address in RAM
        
        pt_entry = ((ppage & PAGE_FRAME) | PT_PRESENT_MASK | PT_DIRTY_MASK);
        as_set_pt_entry(as, faultaddress, pt_entry);
        
        unsigned cmidx = ((ppage & PAGE_FRAME) - user_base_addr) / PAGE_SIZE;
        _coremap[cmidx] |= (faultaddress);

    }
    else {
        pt_entry = (pt_entry & PAGE_FRAME) | PT_PRESENT_MASK | PT_USED_MASK;
        as_set_pt_entry(as, faultaddress, pt_entry);
    }

    uint32_t entryhi, entrylo;
    
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
            lock_release(global_lock);
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
    
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }
        
    if (!as_acquired) {
        spinlock_release(&as->as_lock);
    }

    spinlock_acquire(&tlb_lock);
    
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


    spinlock_release(&tlb_lock);
    lock_release(global_lock);
    return 0;
}

int 
alloc_sbrk_pages(unsigned npages)
{
    struct addrspace *as = proc_getas();
    vaddr_t vaddr;
    unsigned i;
    pagetable_t pte;
    uint32_t ppe;
    paddr_t cmidx;
    pid_t pid = curproc->pid << 6;
    bool acquired = spinlock_do_i_hold(&coremap_lock);

    if (!acquired) {    
        spinlock_acquire(&coremap_lock);
    }
    
    bool as_acquired = spinlock_do_i_hold(&as->as_lock);
    
    if (!as_acquired) {
        spinlock_acquire(&as->as_lock);
    }
    
    vaddr_t heap_top = as->as_heap_top;

    if (nfreepages > npages) {
        for (i=0; i<npages; i++) {
            //vaddr = acquire_pages(1);
            vaddr = acquire_one_page();
            pte = ((vaddr & PAGE_FRAME) | PT_PRESENT_MASK | PT_DIRTY_MASK);
            cmidx = ((vaddr & PAGE_FRAME) - user_base_addr) / PAGE_SIZE;

            // set the coremap entry
            ppe = ((heap_top & PAGE_FRAME) | PP_ALLOC_END | PP_DIRTY | PP_USE | pid);
            _coremap[cmidx] = ppe;
    
            // set the pagetable entry, create pagetable when needed
            as_set_pt_entry(as, heap_top, pte);
            
//            kprintf("vaddr %x, paddr %x\n", heap_top, vaddr); 
            
            heap_top += PAGE_SIZE;
        }
    }

    as->as_heap_top = heap_top;
   
    if (!as_acquired) {
        spinlock_release(&as->as_lock);
    }
    
    if (!acquired) {
        spinlock_release(&coremap_lock);
    }
        
    vm_tlbinvalidate();
    return 0;
}

int 
free_sbrk_pages(unsigned npages)
{
    unsigned i;
    struct addrspace *as = proc_getas();
    vaddr_t heap_top;
    pagetable_t pte;
    uint32_t ppe;
    paddr_t cmidx;
    
    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {    
        spinlock_acquire(&coremap_lock);
    }

    bool as_acquired = spinlock_do_i_hold(&as->as_lock);
    if (!as_acquired) {
        spinlock_acquire(&as->as_lock);
    }
    
    if (npages > 0) {
        heap_top = as->as_heap_top - (npages * PAGE_SIZE);
        as->as_heap_top = heap_top;
        
        for (i=0; i<npages; i++) {
            as_get_pt_entry(as, heap_top, &pte);
            cmidx = ((pte & PAGE_FRAME) - user_base_addr) / PAGE_SIZE;

            // set the coremap entry
            ppe = PP_FREE;
            _coremap[cmidx] = ppe;
    
            // set the pagetable entry
            as_set_pt_entry(as, heap_top, 0);
            
            nfreepages++;
            heap_top += PAGE_SIZE;
        }
    }
    if (!as_acquired) {
        spinlock_release(&as->as_lock);
    }
    
    if (!acquired) {    
        spinlock_release(&coremap_lock);
    }
    
    return 0;
}

static
unsigned
get_free_swap_idx(void)
{
    // Incomplete - need a way to map existing physical pages to swap memory, and virtual addresses to swap memory somehow
    //
    bool acquired = spinlock_do_i_hold(&swapmap_lock);
    if (!acquired) {    
        spinlock_acquire(&swapmap_lock);
    }
    for (unsigned i = 0; i < NUM_SW_PAGES; i++) {
        if (!_swapmap[i].in_use) {
            if (!acquired) {    
                spinlock_release(&swapmap_lock);
            }
            return i;
        }
    }

    // We ran out of swap space
    kprintf("No more swap space\n");
    if (!acquired) {    
        spinlock_release(&swapmap_lock);
    }
    return NO_SWAP_IDX;
}

static
unsigned
find_swap_idx(paddr_t addr, pid_t pid)
{
    bool acquired = spinlock_do_i_hold(&swapmap_lock);
    if (!acquired) {    
        spinlock_acquire(&swapmap_lock);
    }
    for (unsigned i = 0; i < NUM_SW_PAGES; i++) {
        if (_swapmap[i].pid == pid && _swapmap[i].addr == addr && _swapmap[i].in_use) {
            if (!acquired) {    
                spinlock_release(&swapmap_lock);
            }
            return i;
        }
    }

    //kprintf("Page not found in swap\n");
    if (!acquired) {    
        spinlock_release(&swapmap_lock);
    }
    return NO_SWAP_IDX;
}

void
remove_swap_entry(vaddr_t addr, pid_t pid)
{
    bool acquired = spinlock_do_i_hold(&swapmap_lock);
    if (!acquired) {    
        spinlock_acquire(&swapmap_lock);
    }
    for (unsigned i = 0; i < NUM_SW_PAGES; i++) {
        if (_swapmap[i].pid == pid && _swapmap[i].addr == addr && _swapmap[i].in_use) {
            _swapmap[i].in_use = false;
            if (!acquired) {    
                spinlock_release(&swapmap_lock);
            }
            return;
        }
    }

    //kprintf("Page not found in swap\n");
    if (!acquired) {    
        spinlock_release(&swapmap_lock);
    }
    return;
}

// Swapout a page to disk according to swapclock
void
swapout(void)
{
    int result; 
    unsigned i = swapclock + 1;
    if (i >= last_page) {
        i = 0;
    }

    while (i != swapclock) {
        bool acquired = spinlock_do_i_hold(&coremap_lock);
        if (!acquired) {    
            spinlock_acquire(&coremap_lock);
        }

        if (IS_PPAGE_FIXED(_coremap[i])) {
            if (!acquired) {    
                spinlock_release(&coremap_lock);
            }
            goto nextpage;
        }

        if (IS_PPAGE_FREE(_coremap[i])) {
            if (!acquired) {    
                spinlock_release(&coremap_lock);
            }
            goto nextpage;
        }

        unsigned swap_idx = get_free_swap_idx();

        paddr_t paddr_from_page = user_base_addr + (i * PAGE_SIZE);

        struct iovec iov;
        struct uio ku;

        uio_kinit(&iov, &ku, (void *) PADDR_TO_KVADDR(paddr_from_page), PAGE_SIZE, (off_t) swap_idx * PAGE_SIZE, UIO_WRITE);

        _coremap[i] = PP_FREE;
        nfreepages++;

        bool swaplk_acquired = spinlock_do_i_hold(&swapmap_lock);
        if (!swaplk_acquired) {    
            spinlock_acquire(&swapmap_lock);
        }

        _swapmap[swap_idx].in_use = true;
        _swapmap[swap_idx].addr = user_base_addr + (i * PAGE_SIZE);
        _swapmap[swap_idx].pid = (_coremap[i] & PP_PID_MASK) >> 6;

        if (!swaplk_acquired) {    
            spinlock_release(&swapmap_lock);
        }
        
        if (!acquired) {    
            spinlock_release(&coremap_lock);
        }
        
        result = VOP_WRITE(swap_vnode, &ku);
        if (result) {
            panic("ERROR writing to swap diskn\n");
        }

        vm_tlbinvalidate();
        break;

    nextpage:
        i++;
        if (i >= last_page) {
            i = 0; 
        }
    }

    swapclock++;
}

int 
swapin(vaddr_t addr, pid_t pid)
{
    if (nfreepages == 0) {
        kprintf("No free physical pages - cannot swapin\n");
        return SWAPIN_NO_MEM;
    }
    struct iovec iov;
    struct uio ku;
    unsigned start = next_free;

    paddr_t paddr = ((KVADDR_TO_PADDR(addr) & PAGE_FRAME) - user_base_addr) / PAGE_SIZE; 

    unsigned swap_idx = find_swap_idx(paddr, pid);
    if (swap_idx == NO_SWAP_IDX) {
        //kprintf("Address of this process not found in swap\n");
        // Not in swap
        return SWAPIN_NOT_FOUND;
    }

    bool acquired = spinlock_do_i_hold(&coremap_lock);
    if (!acquired) {    
        spinlock_acquire(&coremap_lock);
    }

    //find a free page in physical memory
    for (unsigned i = 0; i < last_page; i++) {
        if (IS_PPAGE_FREE(_coremap[start])) {
            _coremap[start] = (PP_ALLOC_END | PP_CLEAN | PP_USE | pid);
            nfreepages--;
            break;
        }
        start++;
        if (start >= last_page)
            start = 0;
    }


    // TODO - figure out how to access correct swap offset and select correct memory
    uio_kinit(&iov, &ku, (void *) PADDR_TO_KVADDR(paddr), PAGE_SIZE, (off_t) swap_idx * PAGE_SIZE, UIO_READ);

    next_free = start + 1;
    if (next_free >= last_page)
        next_free = 0;

    if (!acquired) {    
        spinlock_release(&coremap_lock);
    }
    
    VOP_READ(swap_vnode, &ku);

    return 0;
}
