/*
    Our system call implementations here
*/
#include <syscall.h>
#include <types.h>
#include <kern/errno.h>
#include <vfs.h>
#include <vnode.h>
#include <lib.h>
#include <current.h>
#include <proc.h>
#include <copyinout.h>
#include <limits.h>
#include <uio.h>
#include <filetable.h>
#include <synch.h>

/*
 * open() system call implementation
 */
int
sys_open(const char *filename, int flags, int *retval) 
{
    *retval = -1;
    struct vnode *opened_file;
    size_t ret_got = 0;
    size_t path_len;
    int fid;

    if(filename == NULL) {
        return EFAULT;
    }

    path_len = strlen(filename)+1;
    char* file_dest  = kmalloc(path_len);

    if(file_dest == NULL) {
        return ENOMEM;
    }

    int err = copyinstr((const_userptr_t)filename, file_dest, path_len, &ret_got);
    
    if(err)
    {
        //error for invalid copyin
        kfree(file_dest);
        //set errno?
        return err;
    }
    
    err = vfs_open(file_dest, flags, 0664, &opened_file);
    if(err)
    {
        //error for invalid copyin
        kfree(file_dest);
        //set errno?
        return err;
    }
    //if successful, add to filetable
    struct ft_file* f;
    f = ft_file_create(opened_file, flags);
    if(f == NULL) {
        kfree(file_dest);
        return ENOMEM;
    }

    err = add_ft_file(curproc->p_ft, f, &fid);
    if(err) {  //checking for EMFILE err
        kfree(file_dest);
        kfree(f);
        return err;  
    }
    *retval = fid;
    
    kfree(file_dest);
    return 0;    
}


/*
 * read() system call implementation
 */
int
sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *retval)
{
    (void) fd; (void) buf; (void) buflen; (void) retval;
    return 0;
}

/*
 * write() system call implementation
 */
int
sys_write(int fd, const userptr_t buf, size_t nbytes, ssize_t *retval)
{
    struct iovec iov;
    struct uio ku;
    char *k_buf;

    if (fd < 0 || fd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }

    // Check whether file entry exists
    lock_acquire(curproc->p_ft->lk_ft);
    if(curproc->p_ft->file_entries[fd] == NULL) {
        lock_release(curproc->p_ft->lk_ft);
        *retval = -1;
        return EBADF;
    }
    lock_release(curproc->p_ft->lk_ft);

    // Copyin data from userspace to a buffer
    k_buf = kmalloc(nbytes);
    if (!k_buf) {
        *retval = -1;
        return ENOMEM;
    }
    if (copyin(buf, k_buf, nbytes)) {
        kfree(k_buf);
        *retval = -1;
        return EFAULT;
    }

    // Check if file is opened for writing
    lock_acquire(curproc->p_ft->file_entries[fd]->lk_file);
    if ((curproc->p_ft->file_entries[fd]->flags & O_WRONLY) != O_WRONLY) {
        lock_release(curproc->p_ft->file_entries[fd]->lk_file);
        kfree(k_buf);
        *retval = -1;
        return EBADF;
    }

    // Create uio block and write to vnode
    uio_kinit(&iov, &ku, k_buf, nbytes, curproc->p_ft->file_entries[fd]->offset, UIO_WRITE);
    
    int result = VOP_WRITE(curproc->p_ft->file_entries[fd]->vn, &ku);
    if (result) {
        lock_release(curproc->p_ft->file_entries[fd]->lk_file);
        kfree(k_buf);
        *retval = -1;
        return result;
    }

    size_t nbytes_written = nbytes - ku.uio_resid;
    curproc->p_ft->file_entries[fd]->offset += nbytes_written;

    lock_release(curproc->p_ft->file_entries[fd]->lk_file);
    kfree(k_buf);
    *retval = nbytes_written;
    
    return 0;
}

/*
 * lseek() system call implementation
 */
int
sys_lseek(int fd, off_t pos, int whence, off_t *retval)
{
    (void) fd; (void) pos; (void) whence; (void) retval;
    return 0;
}

/*
 * close() system call implementation
 */
int
sys_close(int fd, int *retval)
{
    *retval = -1;

    if(fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    struct filetable *ft = curproc->p_ft;
    lock_acquire(ft->lk_ft);
    struct ft_file *f = ft->file_entries[fd];

    //return err if 0 < fd <= OPEN_MAX or there is no open file with file desc fd
    if(f == NULL) {
        lock_release(ft->lk_ft);
        return EBADF;
    }
    lock_acquire(f->lk_file);
    vfs_close(f->vn);
    lock_release(f->lk_file);

    ft_file_destroy(f);
    ft->file_entries[fd] = NULL;
    ft->num_opened--;
    lock_release(ft->lk_ft);

    *retval = 0;
    return 0;
}

/*
 * dup2() system call implementation
 */
int
sys_dup2(int oldfd, int newfd, int *retval)
{
    (void) oldfd; (void) newfd; (void) retval;
    return 0;
}

/*
 * chdir() system call implementation
 */
int
sys_chdir(const userptr_t pathname, int *retval)
{
    char path_str[PATH_MAX];
    size_t path_str_size = 0;
    
    if (copyinstr(pathname, path_str, PATH_MAX, &path_str_size)) {
        *retval = -1;
        return EFAULT;
    }
    
    int result = vfs_chdir(path_str);
    if (result) {
        *retval = -1;
        return result;
    }
        
    *retval = 0;
    return 0;
}

/*
 * __getcwd() system call implementation
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{
    struct iovec iov;
    struct uio ku;
    char k_buf[PATH_MAX] = {0};

    uio_kinit(&iov, &ku, k_buf, PATH_MAX, 0, UIO_READ);
    int result = vfs_getcwd(&ku);
    if (result) {
        *retval = -1;
        return result;
    }

    size_t path_str_size = 0;
    if (copyoutstr(k_buf, buf, buflen, &path_str_size)) {
        *retval = -1;
        return EFAULT;
    }

    *retval = path_str_size;
    return 0;

}
