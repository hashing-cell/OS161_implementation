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

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */

#include <machine/types.h>
#include <machine/vm.h>

struct pagetable;
typedef uint32_t       swapmap_t;

// max physical RAM = 16MB
#define RAM_MAX             (16 * 1024 * 1024)
// 2M for now, maybe physical memory + swap size when swapping is implemented
#define MAX_HEAP            (3 * 1024 * 1024)
#define SWAP_SIZE           (5 * 1024 * 1024)

// total number of ppages = 4096
#define NUM_PPAGES          (RAM_MAX / PAGE_SIZE)
#define NUM_SW_PAGES        (SWAP_SIZE / PAGE_SIZE)
#define MIN_FREE_PAGES      8

// 1 ppage entry uses 8 bytes, 1 page can control PAGE_SIZE / 8 = 512 entries
#define PPAGE_ENTRIES       (PAGE_SIZE / 8)

// number of pages to hold the coremap = 8
#define NUM_COREMAP_PAGES   (NUM_PPAGES / PPAGE_ENTRIES)

// 1 vpage entry uses 4 bytes, 1 page can control PAGE_SIZE / 4 = 1024 entries
#define VPAGE_ENTRIES    (PAGE_SIZE / 4)

#define PAGE_OFFSET_BITS    12

// 6 bits, assume PID_MAX = 64
#define PP_PID_MASK             0xFC0
#define PP_USE_MASK             0x020
#define PP_ALLOC_END_MASK       0x010
#define PP_STATE_MASK           0x00C
#define PP_PERMISSION_MASK      0x003

#define PP_USE                  0x020
#define PP_ALLOC_END            0x010

#define PP_FREE                 0x000
#define PP_DIRTY                0x004        
#define PP_CLEAN                0x008
#define PP_FIXED                0x00C

#define PP_READABLE             0x004
#define PP_WRITEABLE            0x080
#define PP_EXECUTABLE           0x100


#define IS_PPAGE_FREE(ppage)        ((ppage & PP_STATE_MASK) == PP_FREE)
#define IS_PPAGE_IN_RAM(ppage)      ((ppage & PP_STATE_MASK) == PP_DIRTY)
#define IS_PPAGE_IN_SWAP(ppage)     ((ppage & PP_STATE_MASK) == PP_CLEAN)
#define IS_PPAGE_FIXED(ppage)       ((ppage & PP_STATE_MASK) == PP_FIXED)

#define IS_PPAGE_USE(ppentry)       (ppentry & PP_USE)
#define CLEAR_PPAGE_USE(ppentry)    (*ppentry & ~PP_USE)

#define IS_PPAGE_ALLOC_END(ppage)   (ppage & PP_ALLOC_END_MASK)

struct coremap {
//    ppagemap_t cm_ppmap[NUM_PPAGES];
//    uint32_t    *cm_ppmap;
};

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);
int alloc_sbrk_pages(unsigned npages);
int free_sbrk_pages(unsigned npages);
int duplicate_pagetable(struct pagetable* from, struct pagetable *to);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void);
void vm_tlbshootdown(const struct tlbshootdown *);
void vm_tlbinvalidate(void);

#endif /* _VM_H_ */
