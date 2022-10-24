#include <types.h>
#include <vnode.h>
#include <limits.h>
#include <lib.h>
#include <filetable.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <synch.h>
#include <vfs.h>


struct filetable* create_filetable(void) {
    struct filetable *ft;
    ft = kmalloc(sizeof(struct filetable));
    ft->lk_ft = lock_create("filetable lock");
    ft->num_opened = 3;  //stdio & stderr opened
    ft->next_fid = 3;    //1st 3 file desc to be used for stdio & stderr, start at idx 3
    //init_std(ft);
    for(int i = 3; i < OPEN_MAX; i++) {  //setup 1st 3 file desc to be used for stdio & stderr      
        ft->file_entries[i] = NULL;
    } 

    return ft;
}

void init_std(struct filetable* ft) {
    struct vnode *con_vn;
    char path[] = "con:";
    vfs_open(path, O_RDONLY, 0664, &con_vn);
    lock_acquire(ft->lk_ft);
    ft->file_entries[0] = create_ft_file(con_vn, O_RDONLY);
    
    vfs_open(path, O_WRONLY, 0664, &con_vn);
    ft->file_entries[1] = create_ft_file(con_vn, O_WRONLY);
    ft->file_entries[2] = create_ft_file(con_vn, O_WRONLY);
    lock_release(ft->lk_ft);
}

struct ft_file* create_ft_file(struct vnode* v, mode_t in_flags) {
    struct ft_file *f;
    f = kmalloc(sizeof(struct ft_file));
    f->vn = v;
    f->flags = in_flags;
    f->lk_file = lock_create("ft_file lock");
    
    return f;
}

int add_file_entry(struct filetable *ft, struct ft_file *f) {
    if(ft->num_opened < OPEN_MAX) {
        lock_acquire(ft->lk_ft);

        ft->file_entries[ft->next_fid] = f;
        ft->num_opened++;

        if(ft->num_opened < OPEN_MAX) {  //if ft still not full yet, set next_fid
            ft->next_fid++;
            if(ft->next_fid >= OPEN_MAX) {  //reached end of ft, wraparound
                ft->next_fid = 0;

                while(ft->file_entries[ft->next_fid] != NULL) {  //find next free slot
                    ft->next_fid++;
                }
            }
        }

        lock_release(ft->lk_ft);

        return 1;
    }
    return EMFILE;
}






