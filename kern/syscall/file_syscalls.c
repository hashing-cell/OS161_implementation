/*
    Our system call implementations here
*/
#include <syscall.h>
#include <types.h>

/*
 * open() system call implementation
 */
int
sys_open(const userptr_t filename, int flags, int *retval) 
{
    (void) filename; (void) flags; (void) retval;
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
    (void) pathname; (void) retval;
    return 0;
}

/*
 * __getcwd() system call implementation
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{
    (void) buf; (void) buflen; (void) retval;
    return 0;
}
