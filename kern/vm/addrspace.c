/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

// number of entries in a pagetable, PAGE_SIZE / 4
#define NUM_PTE             1024

// number of bits used to represent a page frame number
#define PFN_BITS            10

// number of bits used to represent page offset
#define PAGE_OFFSET_BITS    12

// page frame number mask
#define PFN_MASK            0x3FF



static
struct pagetable*
create_pagetable()
{
    struct pagetable *new_pt = kmalloc(PAGE_SIZE);
    if (new_pt) 
        bzero(new_pt, PAGE_SIZE);
    return new_pt;
}

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	
	bzero(as, sizeof(struct addrspace));

	/*
	 * Initialize as needed.
	 */
    as->as_stack_top = USERSTACK;
	as->as_stack_base = USERSTACK - STACK_SIZE;
	
	as->as_stack_permission = AS_READABLE | AS_WRITEABLE;
	as->as_heap_permission = AS_READABLE| AS_WRITEABLE;
	as->as_code_permission = AS_READABLE | AS_WRITEABLE;
	as->as_data_permission = AS_READABLE | AS_WRITEABLE;
	
	as->as_kpages = as->as_vpages = 0;
	as->as_kpagesreleased = as->as_vpagesreleased = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}
	
	newas->as_code_base = old->as_code_base;
	newas->as_code_top = old->as_code_top;	
	newas->as_data_base = old->as_data_base;
	newas->as_data_top = old->as_data_top;	
	newas->as_stack_base = old->as_stack_base;
	newas->as_stack_top = old->as_stack_top;	
	newas->as_heap_base = old->as_heap_base;
	newas->as_heap_top = old->as_heap_top;	
	
    int i, j;
    struct pagetable* oldpt;
    struct pagetable* newpt;

    for (i=0; i<NUM_PTE; i++) {
        oldpt = (struct pagetable *) (old->as_pagedir[i] & PAGE_FRAME);
        if (oldpt != NULL) {
            newpt = create_pagetable();
            if (newpt == NULL) {
                as_destroy(newas);
                return ENOMEM;
            }
            // add the new table to new page directory
            newas->as_pagedir[i] = (pagedir_t) ((vaddr_t) newpt & PAGE_FRAME);
            
            // iterate through the pagetable and populate entries
            for (j=0; j<NUM_PTE; j++) {
                if (oldpt->pt_entries[j] != 0) {
                    newpt->pt_entries[j] = oldpt->pt_entries[j];
                }
            }
        }
    }

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
    struct pagetable *pt;
    paddr_t ppage;
    int i, j;
    for (i=0; i<PFN_BITS; i++) {
        pt = (struct pagetable *) (as->as_pagedir[i] & PAGE_FRAME);  
        as->as_pagedir[i] = 0;          
        if (pt != NULL) {
            for (j=0; j<PFN_BITS; j++) {
                ppage = (paddr_t) (pt->pt_entries[j] & PAGE_FRAME);
                if (ppage > 0) {
                    // we store in paddr_t, but free_kpages is expecting vaddr_t
                    free_kpages(PADDR_TO_KVADDR(ppage));
                }
            }
            
            kfree(pt);
        }
    }
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

    vm_tlbinvalidate();
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
    size_t npages;
    __u32 permission = (readable | writeable | executable) << 8;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;
    
	if (as->as_code_base == 0) {
	    as->as_code_base = vaddr;
	    as->as_code_top = vaddr + npages * PAGE_SIZE;
	    as->as_code_permission |= permission; 

	    as->as_heap_base = as->as_heap_top = as->as_code_top;
	}
	else {
	    if (vaddr >= as->as_code_top) {
	        as->as_data_base = vaddr;
	        as->as_data_top = vaddr + npages * PAGE_SIZE;
	        as->as_data_permission |= permission;
	        as->as_heap_base = as->as_heap_top = as->as_data_top;
	    }
	    else {
	        // the data segment was loaded before text segment
	        as->as_data_base = as->as_code_base;
	        as->as_data_top = as->as_code_top;
	        as->as_data_permission = as->as_code_permission;
	        
	        as->as_code_base = vaddr;
	        as->as_code_top = vaddr + npages * PAGE_SIZE;
	        as->as_code_permission |= permission;
	        
	        as->as_heap_base = as->as_heap_top = as->as_data_top;
	    }
	}
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
 	(void)as;
 	
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
    as->as_code_permission >>= 8;
    as->as_data_permission >>= 8;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    // no implementation
	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

int
as_get_pt_entry(struct addrspace* as, vaddr_t addr, pagetable_t *pt_entry) 
{
    unsigned pd_idx = (addr >> (PFN_BITS + PAGE_OFFSET_BITS));
    unsigned pt_idx = ((addr >> PAGE_OFFSET_BITS) & PFN_MASK);
    
    pagedir_t pde = as->as_pagedir[pd_idx];
    vaddr_t ptaddr; 
    
    if (pde == 0) {
        ptaddr = (vaddr_t) create_pagetable();
        if (ptaddr == 0) {
            return ENOMEM;             
        }
        // add the new pagetable to page directory
        // upper 20 bits for pagetable address
        as->as_pagedir[pd_idx] = (pagedir_t) (ptaddr & PAGE_FRAME);
    }
    else {
        ptaddr = (pde & PAGE_FRAME);
    }
    
    *pt_entry = ((struct pagetable *) ptaddr)->pt_entries[pt_idx];
    return 0;
}

int
as_set_pt_entry(struct addrspace *as, vaddr_t addr, pagetable_t pt_entry)
{
    unsigned pd_idx = addr >> (PAGE_OFFSET_BITS + PFN_BITS);
    unsigned pt_idx = (addr >> PAGE_OFFSET_BITS) & PFN_MASK;

    pagedir_t pde = as->as_pagedir[pd_idx];
    struct pagetable *pt = (struct pagetable*) (pde & PAGE_FRAME);
    if (pt == NULL) {
        pt = create_pagetable();
    }
    
    KASSERT(pt != NULL);
    pt->pt_entries[pt_idx] = pt_entry;

    return 0;
}

bool
as_is_valid_address(struct addrspace* as, vaddr_t addr)
{
    KASSERT(as != NULL);
    if (addr >= as->as_code_base && addr < as->as_code_top) {
        return true;
    }
    
    if (addr >= as->as_data_base && addr < as->as_data_top) {
        return true;
    }
    
    if (addr >= as->as_stack_base && addr < as->as_stack_top) {
        return true;
    }
    
    if (addr >= as->as_heap_base && addr < as->as_heap_top) {
        return true;
    }
    
    return false;
}

unsigned
as_get_permission(struct addrspace *as, vaddr_t addr)
{
    KASSERT(as != NULL);
    if (addr >= as->as_code_base && addr < as->as_code_top) {
        return as->as_code_permission;
    }
    
    if (addr >= as->as_data_base && addr < as->as_data_top) {
        return as->as_data_permission;
    }
    
    if (addr >= as->as_stack_base && addr < as->as_stack_top) {
        return as->as_stack_permission;
    }
    
    if (addr >= as->as_heap_base && addr >= as->as_heap_top) {
        return as->as_heap_permission;
    }
    
    return 0;
}
