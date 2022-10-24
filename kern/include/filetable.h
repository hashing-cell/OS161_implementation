#include <types.h>
#include <vnode.h>
#include <limits.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>

struct ft_file {
    struct vnode *vn;
    int flags;
    off_t offset;
    struct lock* lk_file;
};

struct filetable {
    struct ft_file* file_entries[OPEN_MAX]; //OPEN_MAX = max # files per process that can be opened
    int num_opened;
    int next_fid;
    struct lock* lk_ft;
};

struct filetable* filetable_create(void);
void filetable_destroy(struct filetable*);
struct ft_file* ft_file_create(struct vnode*, int);
void ft_file_destroy(struct ft_file*);
void filetable_destroy(struct filetable* ft);
int add_ft_file(struct filetable*, struct ft_file*);  //decide how to add files to ft, add to end or fill in holes first?
void init_stdio(struct filetable*);

