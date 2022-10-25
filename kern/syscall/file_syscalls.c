/*
    Our system call implementations here
*/
#include <syscall.h>
#include <types.h>
#include <kern/errno.h>
#include <kern/stat.h>
#include <kern/seek.h>
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
    struct iovec iov;
    struct uio ku;
    char *k_buf;

    if (buf == NULL) {
        *retval = -1;
        return EFAULT;
    }

    // Check if fd is a valid one
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

    // Check if file is opened for reading
    lock_acquire(curproc->p_ft->file_entries[fd]->lk_file);
    int flags_masked = curproc->p_ft->file_entries[fd]->flags & 0x03; //bitmask for the last 2 bits
    if (flags_masked == O_WRONLY) {
        lock_release(curproc->p_ft->file_entries[fd]->lk_file);
        *retval = -1;
        return EBADF;
    }

    // Allocate memory and create uio block and read from vnode associated with file
    k_buf = kmalloc(buflen);
    if (!k_buf) {
        lock_release(curproc->p_ft->file_entries[fd]->lk_file);
        *retval = -1;
        return ENOMEM;
    }
    uio_kinit(&iov, &ku, k_buf, buflen, curproc->p_ft->file_entries[fd]->offset, UIO_READ);
    
    // Do the actual vnode read
    int result = VOP_READ(curproc->p_ft->file_entries[fd]->vn, &ku);
    if (result) {
        lock_release(curproc->p_ft->file_entries[fd]->lk_file);
        kfree(k_buf);
        *retval = -1;
        return result;
    }

    // Check how much bytes have been actually written, and advance seek position
    size_t nbytes_read = buflen - ku.uio_resid;
    curproc->p_ft->file_entries[fd]->offset += nbytes_read;

    // Copyout to user provided buffer
    if (copyout(k_buf, buf, nbytes_read)) {
        lock_release(curproc->p_ft->file_entries[fd]->lk_file);
        kfree(k_buf);
        *retval = -1;
        return EFAULT;
    }

    lock_release(curproc->p_ft->file_entries[fd]->lk_file);
    kfree(k_buf);
    *retval = nbytes_read;

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

    if (buf == NULL) {
        *retval = -1;
        return EFAULT;
    }

    // Check if fd is a valid one
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
    int flags_masked = curproc->p_ft->file_entries[fd]->flags & 0x03; //bitmask for the last 2 bits
    if (flags_masked == O_RDONLY) {
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

    // Check how much bytes have been actually written, and advance seek position
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
    *retval = -1;
    
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return EINVAL;
    }
    
    if (fd < 0 || fd >= OPEN_MAX) {
        return EINVAL;
    }
    
    struct filetable *ft;
    struct ft_file *f;
    struct stat stat;
    off_t new_pos;
    
    ft = curproc->p_ft;
    if (ft == NULL) {
        return EFAULT;
    }
    
    lock_acquire(ft->lk_ft);
    f = ft->file_entries[fd];
    if (f == NULL) {
        lock_release(ft->lk_ft);
        return EFAULT;
    }

    lock_release(ft->lk_ft);    
    lock_acquire(f->lk_file);    
    
    if (!VOP_ISSEEKABLE(f->vn)) {
        lock_release(f->lk_file);
        return ESPIPE;
    }

    VOP_STAT(f->vn, &stat);
    
    switch (whence) {
        case (SEEK_SET):
            new_pos = pos;
            break;
            
        case (SEEK_CUR):
            new_pos = f->offset + pos;
            break;
            
        case (SEEK_END):
            new_pos = stat.st_size + pos;
            break;
    }
    
    if (new_pos < 0) {
        lock_release(f->lk_file);
        return EINVAL;
    }
    
    f->offset = new_pos;
    lock_release(f->lk_file); 

    *retval = new_pos;
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
    *retval = -1;
    
    if (oldfd < 0 || oldfd >= OPEN_MAX) {
        return EBADF;
    }
    
    if (newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }
    
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }
    
    struct filetable *ft;
    struct ft_file *old_ft_file;
    struct ft_file *new_ft_file;
    struct ft_file *cloned_ft_file;
    
    ft = curproc->p_ft;
    if (ft == NULL) {
        return EFAULT;
    }
    
    lock_acquire(ft->lk_ft);
    old_ft_file = ft->file_entries[oldfd];
    new_ft_file = ft->file_entries[newfd];
    
    if (old_ft_file == NULL) {
        lock_release(ft->lk_ft);
        return EFAULT;
    }
    
    lock_acquire(old_ft_file->lk_file);
    cloned_ft_file = ft_file_create(old_ft_file->vn, old_ft_file->flags);
    if (cloned_ft_file == NULL) {
        lock_release(old_ft_file->lk_file);
        lock_release(ft->lk_ft);
        return ENOMEM;
    }

    cloned_ft_file->offset = old_ft_file->offset;
    lock_release(old_ft_file->lk_file);

    if (new_ft_file != NULL) {
        // already opened file.  Close it silently
        ft_file_destroy(new_ft_file);
        ft->num_opened--;
        ft->file_entries[newfd] = NULL;
    }

    ft->file_entries[newfd] = cloned_ft_file;    
    VOP_INCREF(cloned_ft_file->vn);
    lock_release(ft->lk_ft);
    
    *retval = newfd;
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
