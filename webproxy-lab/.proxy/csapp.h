/* csapp.h - CS:APP3e support header (matching your csapp.c) */
#ifndef __CSAPP_H__
#define __CSAPP_H__

/* ==== System headers ==== */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/mman.h>
#include <dirent.h>

#include <pthread.h>
#include <semaphore.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* ==== Misc convenience ==== */
typedef void handler_t(int);
typedef struct sockaddr SA;
extern char **environ;

/* ==== Constants used by Tiny/Proxy ==== */
#define MAXLINE 8192 /* max text line length */
#define MAXBUF 8192  /* max I/O buffer size */
#define LISTENQ 1024 /* backlog for listen() */

/* ==== Robust I/O buffer ==== */
#define RIO_BUFSIZE 8192
typedef struct
{
    int rio_fd;                /* descriptor for this internal buf */
    int rio_cnt;               /* unread bytes in internal buf */
    char *rio_bufptr;          /* next unread byte in internal buf */
    char rio_buf[RIO_BUFSIZE]; /* internal buffer */
} rio_t;

/* ============================================================
 * Error handling helpers (process-terminating; feel free to adapt in proxy)
 * ============================================================ */
void unix_error(char *msg);
void posix_error(int code, char *msg);
void gai_error(int code, char *msg);
void app_error(char *msg);
void dns_error(char *msg);

/* ============================================================
 * Process control
 * ============================================================ */
pid_t Fork(void);
void Execve(const char *filename, char *const argv[], char *const envp[]);
pid_t Wait(int *status);
pid_t Waitpid(pid_t pid, int *iptr, int options);
void Kill(pid_t pid, int signum);
void Pause(void);
unsigned int Sleep(unsigned int secs);
unsigned int Alarm(unsigned int seconds);
void Setpgid(pid_t pid, pid_t pgid);
pid_t Getpgrp(void);

/* ============================================================
 * Signals
 * ============================================================ */
handler_t *Signal(int signum, handler_t *handler);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigemptyset(sigset_t *set);
void Sigfillset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
void Sigdelset(sigset_t *set, int signum);
int Sigismember(const sigset_t *set, int signum);
int Sigsuspend(const sigset_t *set);

/* ============================================================
 * SIO (signal-safe I/O) wrappers
 * ============================================================ */
ssize_t Sio_putl(long v);
ssize_t Sio_puts(char s[]);
void Sio_error(char s[]);

/* ============================================================
 * Unix I/O wrappers
 * ============================================================ */
int Open(const char *pathname, int flags, mode_t mode);
ssize_t Read(int fd, void *buf, size_t count);
ssize_t Write(int fd, const void *buf, size_t count);
off_t Lseek(int fildes, off_t offset, int whence);
void Close(int fd);
int Select(int n, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
int Dup2(int fd1, int fd2);
void Stat(const char *filename, struct stat *buf);
void Fstat(int fd, struct stat *buf);

/* ============================================================
 * Directory wrappers
 * ============================================================ */
DIR *Opendir(const char *name);
struct dirent *Readdir(DIR *dirp);
int Closedir(DIR *dirp);

/* ============================================================
 * Memory mapping
 * ============================================================ */
void *Mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
void Munmap(void *start, size_t length);

/* ============================================================
 * Heap allocation wrappers
 * ============================================================ */
void *Malloc(size_t size);
void *Realloc(void *ptr, size_t size);
void *Calloc(size_t nmemb, size_t size);
void Free(void *ptr);

/* ============================================================
 * Standard I/O wrappers
 * ============================================================ */
void Fclose(FILE *fp);
FILE *Fdopen(int fd, const char *type);
char *Fgets(char *ptr, int n, FILE *stream);
FILE *Fopen(const char *filename, const char *mode);
void Fputs(const char *ptr, FILE *stream);
size_t Fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
void Fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* ============================================================
 * Sockets interface wrappers
 * ============================================================ */
int Socket(int domain, int type, int protocol);
void Setsockopt(int s, int level, int optname, const void *optval, int optlen);
void Bind(int sockfd, struct sockaddr *my_addr, int addrlen);
void Listen(int s, int backlog);
int Accept(int s, struct sockaddr *addr, socklen_t *addrlen);
void Connect(int sockfd, struct sockaddr *serv_addr, int addrlen);

/* ============================================================
 * Protocol-independent helpers
 * ============================================================ */
void Getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host,
                 size_t hostlen, char *serv, size_t servlen, int flags);
void Inet_ntop(int af, const void *src, char *dst, socklen_t size);
void Inet_pton(int af, const char *src, void *dst);

/* ============================================================
 * (Obsolete, non-thread-safe) DNS helpers
 * ============================================================ */
struct hostent *Gethostbyname(const char *name);
struct hostent *Gethostbyaddr(const char *addr, int len, int type);

/* ============================================================
 * Pthreads wrappers
 * ============================================================ */
void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp,
                    void *(*routine)(void *), void *argp);
void Pthread_cancel(pthread_t tid);
void Pthread_join(pthread_t tid, void **thread_return);
void Pthread_detach(pthread_t tid);
void Pthread_exit(void *retval);
pthread_t Pthread_self(void);
void Pthread_once(pthread_once_t *once_control, void (*init_function)());

/* ============================================================
 * POSIX semaphores
 * ============================================================ */
void Sem_init(sem_t *sem, int pshared, unsigned int value);
void P(sem_t *sem);
void V(sem_t *sem);

/* ============================================================
 * Robust I/O (RIO) — unbuffered & buffered
 * (lowercase = internal; uppercase = error-handling wrappers)
 * ============================================================ */
/* Unbuffered robust I/O */
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);

/* Buffered robust I/O */
void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

/* Error-handling wrappers for robust I/O */
ssize_t Rio_readn(int fd, void *ptr, size_t nbytes);
void Rio_writen(int fd, void *usrbuf, size_t n);
void Rio_readinitb(rio_t *rp, int fd);
ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

/* ============================================================
 * Client/server helpers (protocol-independent)
 * ============================================================ */
int open_clientfd(char *hostname, char *port);
int open_listenfd(char *port);
int Open_clientfd(char *hostname, char *port);
int Open_listenfd(char *port);

#endif /* __CSAPP_H__ */
