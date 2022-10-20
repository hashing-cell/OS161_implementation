/*
    Our system call implementations here
*/
#include <syscall.h>
#include <types.h>

/*
 * open() system call implementation
 */
int
sys_open(const char *filename, int flags) 
{
    //stuff    
}


/*
 * read() system call implementation
 */
ssize_t
sys_read(int fd, void *buf, size_t buflen)
{
    //stuff

}

/*
 * write() system call implementation
 */
ssize_t
sys_write(int fd, const void *buf, size_t nbytes)
{
    //stuff
}

/*
 * lseek() system call implementation
 */
off_t
sys_lseek(int fd, off_t pos, int whence)
{
    //stuff
}

/*
 * close() system call implementation
 */
int
sys_close(int fd)
{
    //stuff
}

/*
 * dup2() system call implementation
 */
int
sys_dup2(int oldfd, int newfd)
{
    //stuff
}

/*
 * chdir() system call implementation
 */
int
sys_chdir(const char *pathname)
{
    //stuff
}

/*
 * __getcwd() system call implementation
 */
int
sys___getcwd(char *buf, size_t buflen)
{
    //stuff
}
