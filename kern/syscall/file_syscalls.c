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
#include <copyinout.h>
#include <limits.h>
#include <uio.h>

/*
 * open() system call implementation
 */
int
sys_open(const char *filename, int flags, int *retval) 
{
    //(void) filename; (void) flags; (void) retval;
    *retval = -1;
    struct vnode *opened_file;
    size_t ret_got = 0;
    size_t path_len;

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
        return -1;
    }
    
    err = vfs_open(file_dest, flags, 0664, &opened_file);
    if(err)
    {
        //error for invalid copyin
        kfree(file_dest);
        //set errno?
        return -1;
    }
    //if successful, add to filetable
    struct ft_file* f;
    f = ft_file_create(opened_file, flags);
    add_ft_file(curproc->p_ft, f);
    *retval = 0;
    
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
    (void) fd; (void) buf; (void) nbytes; (void) retval;
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
    (void) fd; (void) retval;
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
    
    if (!copyinstr(pathname, path_str, PATH_MAX, &path_str_size)) {
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
    if (!copyoutstr(k_buf, buf, buflen, &path_str_size)) {
        *retval = -1;
        return EFAULT;
    }

    *retval = path_str_size;
    return 0;

}
