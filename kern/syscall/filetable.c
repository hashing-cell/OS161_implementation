#include <types.h>
#include <vnode.h>
#include <limits.h>
#include <lib.h>
#include <filetable.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <synch.h>
#include <vfs.h>


struct filetable* filetable_create(void) {
    struct filetable *ft;
    ft = kmalloc(sizeof(struct filetable));
    ft->lk_ft = lock_create("filetable lock");
    ft->num_opened = 3;  //stdio & stderr opened
    ft->next_fid = 3;    //1st 3 file desc to be used for stdio & stderr, start at idx 3
    for(int i = 3; i < OPEN_MAX; i++) {  //setup 1st 3 file desc to be used for stdio & stderr      
        ft->file_entries[i] = NULL;
    } 

    return ft;
}

void filetable_dup(const struct filetable* old_ft, struct filetable *new_ft) {
    lock_acquire(old_ft->lk_ft);
    new_ft->num_opened = old_ft->num_opened;
    new_ft->next_fid = old_ft->next_fid;
    for (int i = 0; i < OPEN_MAX; i++) {
        if (old_ft->file_entries[i] == NULL) {
            continue;
        }
        lock_acquire(old_ft->file_entries[i]->lk_file);
        new_ft->file_entries[i] = old_ft->file_entries[i];
        lock_release(old_ft->file_entries[i]->lk_file);
    }
    lock_release(old_ft->lk_ft);
}

void init_stdio(struct filetable* ft) {
    struct vnode *con_vn;
    char path[] = "con:";

    //stdin is entry 0 with flag O_RDONLY
    vfs_open(path, O_RDONLY, 0664, &con_vn);
    lock_acquire(ft->lk_ft);
    ft->file_entries[0] = ft_file_create(con_vn, O_RDONLY);
    
    vfs_open(path, O_WRONLY, 0664, &con_vn);
    //stdout is entry 1 with flag O_WRONLY
    ft->file_entries[1] = ft_file_create(con_vn, O_WRONLY);
    //stderr is entry 1 with flag O_WRONLY
    ft->file_entries[2] = ft_file_create(con_vn, O_WRONLY);
    lock_release(ft->lk_ft);
}

struct ft_file* ft_file_create(struct vnode* v, int in_flags) {
    struct ft_file *f;
    f = kmalloc(sizeof(struct ft_file));

    if(f == NULL) {
        return NULL;
    }

    f->vn = v;
    f->offset = 0;
    f->flags = in_flags;
    f->lk_file = lock_create("ft_file lock");
    
    return f;
}

void ft_file_destroy(struct ft_file* f) {
    KASSERT(f != NULL);

    lock_destroy(f->lk_file);
    kfree(f);
}

//return error code or 0 if succeeded
//return file desc in fid param
int add_ft_file(struct filetable *ft, struct ft_file *f, int *fid) {
    if(ft->num_opened >= OPEN_MAX) {
        return EMFILE;
    }

    lock_acquire(ft->lk_ft);

    if(ft->file_entries[ft->next_fid] != NULL) {
        for(int i = 0; i < OPEN_MAX-3; i++) {
            ft->next_fid++;
            if(ft->next_fid >= OPEN_MAX) {  //reached end of ft, wraparound
                ft->next_fid = 3;
            }
            if(ft->file_entries[ft->next_fid] == NULL) {
                break;
            }
        }
    }
    ft->file_entries[ft->next_fid] = f;
    
    *fid = ft->next_fid;
    ft->num_opened++;
    ft->next_fid++;
    if(ft->next_fid >= OPEN_MAX) {  //reached end of ft, wraparound to find next avail fid
        ft->next_fid = 3;
    }
    lock_release(ft->lk_ft);

    return 0;
}

void filetable_destroy(struct filetable* ft) {
    KASSERT(ft != NULL);

    lock_destroy(ft->lk_ft);

    for(int i = 0; i < OPEN_MAX; i++) {
        ft_file_destroy(ft->file_entries[i]);
    }

    kfree(ft);
}






