#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <vnode.h>
#include <limits.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>

/* File handle object */
struct ft_file {
    struct vnode *vn;
    int flags;
    off_t offset;
    int refcount;
    struct lock* lk_file; 
};

struct filetable {
    struct ft_file* file_entries[OPEN_MAX]; //OPEN_MAX = max # files per process that can be opened
    int num_opened;
    int next_fid;
    struct lock* lk_ft;
};

struct filetable* filetable_create(void);
void filetable_dup(const struct filetable*, struct filetable *);
void filetable_destroy(struct filetable*);
struct ft_file* ft_file_create(struct vnode*, int);
void ft_file_destroy(struct ft_file*);
void filetable_destroy(struct filetable* ft);
/* Add file object to file_entries of filetable */
int add_ft_file(struct filetable*, struct ft_file*, int*);
/* Decrement reference count of a file, Use this instead of destroying it instead */
void decre_ft_file(struct ft_file*);
/* Initialize stdin, stdout, and stderr in filetable */
void init_stdio(struct filetable*);

#endif