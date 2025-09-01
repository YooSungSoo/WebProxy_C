#ifndef CSAPP_H
#define CSAPP_H

/* --- 기능 매크로: 시스템 헤더 포함 전에 정의 --- */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L /* POSIX.1-2001 (getaddrinfo 등) */
#endif

/* --- 표준/시스템 헤더 --- */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <netdb.h> /* struct addrinfo, getaddrinfo, AI_*, NI_* */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

/* C++ 호환 */
#ifdef __cplusplus
extern "C"
{
#endif

/* --- 공용 상수/매크로 --- */
#ifndef MAXLINE
#define MAXLINE 8192 /* 텍스트 라인 최대 길이 */
#endif

#ifndef RIO_BUFSIZE
#define RIO_BUFSIZE 8192 /* Robust I/O 내부 버퍼 크기 */
#endif

#ifndef LISTENQ
#define LISTENQ 1024 /* listen(2) 대기열 크기 */
#endif

    /* sockaddr 별칭 */
    typedef struct sockaddr SA;

    /* --- Robust I/O 패키지 --- */
    typedef struct
    {
        int rio_fd;                /* 내부에서 사용하는 파일 디스크립터 */
        int rio_cnt;               /* 내부 버퍼에 남은 바이트 수 */
        char *rio_bufptr;          /* 내부 버퍼에서 다음 읽기 위치 */
        char rio_buf[RIO_BUFSIZE]; /* 내부 버퍼 */
    } rio_t;

    /* 저수준 robust I/O 함수 (소문자: 내부 구현) */
    ssize_t rio_readn(int fd, void *usrbuf, size_t n);
    ssize_t rio_writen(int fd, const void *usrbuf, size_t n);
    void rio_readinitb(rio_t *rp, int fd);
    ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
    ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

    /* 대문자 래퍼(에러 처리 포함) */
    ssize_t Rio_readn(int fd, void *usrbuf, size_t n);
    ssize_t Rio_writen(int fd, const void *usrbuf, size_t n);
    void Rio_readinitb(rio_t *rp, int fd);
    ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n);
    ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

    /* --- 네트워킹 헬퍼 --- */
    int Open_clientfd(const char *hostname, const char *port);
    int Open_listenfd(const char *port);

    /* --- 에러 처리 루틴 --- */
    void unix_error(const char *msg);            /* UNIX 에러용 */
    void posix_error(int code, const char *msg); /* POSIX 반환코드용 */
    void gai_error(int code, const char *msg);   /* getaddrinfo 계열 에러용 */
    void app_error(const char *msg);             /* 일반 애플리케이션 에러 */

    /* --- 시스템 콜 래퍼 (선택적으로 사용) --- */
    /* stdio 래퍼 */
    char *Fgets(char *s, int size, FILE *stream);
    void Fputs(const char *s, FILE *stream);

    /* 파일 디스크립터 I/O */
    ssize_t Read(int fd, void *buf, size_t nbyte);
    ssize_t Write(int fd, const void *buf, size_t nbyte);
    off_t Lseek(int fd, off_t offset, int whence);
    int Close(int fd);
    int Dup2(int oldfd, int newfd);

    /* 파일/상태 */
    int Stat(const char *pathname, struct stat *statbuf);
    int Fstat(int fd, struct stat *statbuf);

    /* select */
    int Select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout);

    /* 시그널 */
    typedef void handler_t(int);
    handler_t *Signal(int signum, handler_t *handler);

    /* --- 소켓/네트워크 래퍼 --- */
    int Socket(int domain, int type, int protocol);
    void Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    void Listen(int sockfd, int backlog);
    int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    void Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    int Setsockopt(int sockfd, int level, int optname,
                   const void *optval, socklen_t optlen);

    /* 주소 정보 관련(getaddrinfo / getnameinfo) */
    int Getaddrinfo(const char *node, const char *service,
                    const struct addrinfo *hints, struct addrinfo **res);
    void Freeaddrinfo(struct addrinfo *res);
    int Getnameinfo(const struct sockaddr *sa, socklen_t salen,
                    char *host, size_t hostlen,
                    char *serv, size_t servlen, int flags);

#ifdef __cplusplus
}
#endif

#endif /* CSAPP_H */
