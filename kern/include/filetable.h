#include <types.h>
#include <vnode.h>
#include <limits.h>
#include <lib.h>

struct ft_file {
    struct vnode *vn;
    mode_t flags;
    off_t offset;
    struct lock* lk_file;
};

struct filetable {
    struct ft_file* file_entries[OPEN_MAX];
};

int create_file_entry(struct vnode *vn, mode_t flags, off_t offset);
int destroy_file_entry(struct ft_file*);
int add_file_entry(struct ft_file*);