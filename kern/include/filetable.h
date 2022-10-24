#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <vnode.h>
#include <limits.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>

struct ft_file {
    struct vnode *vn;
    mode_t flags;
    off_t offset;
    struct lock* lk_file;
};

struct filetable {
    struct ft_file* file_entries[OPEN_MAX]; //OPEN_MAX = max # files per process that can be opened
    int num_opened;
    int next_fid;
    struct lock* lk_ft;
};

struct filetable* create_filetable(void);
struct ft_file* create_ft_file(struct vnode*, mode_t);
int destroy_filetable(void);
int add_file_entry(struct filetable*, struct ft_file*);  //decide how to add files to ft, add to end or fill in holes first?
void init_std(struct filetable*);

#endif