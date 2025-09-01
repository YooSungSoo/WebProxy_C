// csapp.c
#include "csapp.h"
#include <string.h>

/* ============================================================
 * 에러 처리 루틴
 * ============================================================ */
void unix_error(const char *msg)
{
    perror(msg);
    exit(1);
}

void posix_error(int code, const char *msg)
{
    errno = code;
    perror(msg);
    exit(1);
}

void gai_error(int code, const char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, gai_strerror(code));
    exit(1);
}

void app_error(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/* ============================================================
 * stdio 래퍼
 * ============================================================ */
char *Fgets(char *s, int size, FILE *stream)
{
    char *p = fgets(s, size, stream);
    if (!p && ferror(stream))
        unix_error("Fgets error");
    return p;
}

void Fputs(const char *s, FILE *stream)
{
    if (fputs(s, stream) == EOF)
        unix_error("Fputs error");
}

/* ============================================================
 * 파일 디스크립터 I/O 래퍼 (필요 최소)
 * ============================================================ */
int Close(int fd)
{
    int rc = close(fd);
    if (rc < 0)
        unix_error("Close error");
    return rc;
}

/* ============================================================
 * Robust I/O (저수준)
 * ============================================================ */
ssize_t rio_readn(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = (char *)usrbuf;

    while (nleft > 0)
    {
        if ((nread = read(fd, bufp, nleft)) < 0)
        {
            if (errno == EINTR)
                nread = 0; /* interrupted by sig handler, retry */
            else
                return -1;
        }
        else if (nread == 0)
        {
            break; /* EOF */
        }
        nleft -= (size_t)nread;
        bufp += nread;
    }
    return (ssize_t)(n - nleft);
}

ssize_t rio_writen(int fd, const void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten;
    const char *bufp = (const char *)usrbuf;

    while (nleft > 0)
    {
        if ((nwritten = write(fd, bufp, nleft)) <= 0)
        {
            if (errno == EINTR)
                nwritten = 0; /* retry */
            else
                return -1;
        }
        nleft -= (size_t)nwritten;
        bufp += nwritten;
    }
    return (ssize_t)n;
}

/* ============================================================
 * Robust I/O (버퍼드)
 * ============================================================ */
void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;

    while (rp->rio_cnt <= 0)
    { /* refill */
        rp->rio_cnt = (int)read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0)
        {
            if (errno != EINTR)
                return -1;
        }
        else if (rp->rio_cnt == 0)
        {
            return 0; /* EOF */
        }
        else
        {
            rp->rio_bufptr = rp->rio_buf; /* reset buffer ptr */
        }
    }

    cnt = (int)n;
    if (rp->rio_cnt < cnt)
        cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, (size_t)cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}

ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = (char *)usrbuf;

    while (nleft > 0)
    {
        if ((nread = rio_read(rp, bufp, nleft)) < 0)
            return -1;
        else if (nread == 0)
            break; /* EOF */
        nleft -= (size_t)nread;
        bufp += nread;
    }
    return (ssize_t)(n - nleft);
}

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    int n, rc;
    char c, *bufp = (char *)usrbuf;

    for (n = 1; n < (int)maxlen; n++)
    {
        if ((rc = (int)rio_read(rp, &c, 1)) == 1)
        {
            *bufp++ = c;
            if (c == '\n')
                break;
        }
        else if (rc == 0)
        {
            if (n == 1)
                return 0; /* no data read */
            break;        /* some data was read */
        }
        else
        {
            return -1; /* error */
        }
    }
    *bufp = '\0';
    return n;
}

/* ============================================================
 * Robust I/O 대문자 래퍼 (에러 처리 포함)
 * ============================================================ */
ssize_t Rio_readn(int fd, void *usrbuf, size_t n)
{
    ssize_t rc = rio_readn(fd, usrbuf, n);
    if (rc < 0)
        unix_error("Rio_readn error");
    return rc;
}
ssize_t Rio_writen(int fd, const void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != (ssize_t)n)
        unix_error("Rio_writen error");
    return (ssize_t)n;
}
void Rio_readinitb(rio_t *rp, int fd) { rio_readinitb(rp, fd); }
ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc = rio_readnb(rp, usrbuf, n);
    if (rc < 0)
        unix_error("Rio_readnb error");
    return rc;
}
ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc = rio_readlineb(rp, usrbuf, maxlen);
    if (rc < 0)
        unix_error("Rio_readlineb error");
    return rc;
}

/* ============================================================
 * 주소/소켓 관련 대문자 래퍼
 * ============================================================ */
int Getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    int rc = getaddrinfo(node, service, hints, res);
    if (rc != 0)
        gai_error(rc, "getaddrinfo");
    return rc;
}

void Freeaddrinfo(struct addrinfo *res)
{
    freeaddrinfo(res);
}

int Setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen)
{
    int rc = setsockopt(sockfd, level, optname, optval, optlen);
    if (rc < 0)
        unix_error("setsockopt");
    return rc;
}

int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int rc;
    do
    {
        rc = accept(sockfd, addr, addrlen);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0)
        unix_error("Accept error");
    return rc;
}

int Getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen, int flags)
{
    int rc = getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
    if (rc != 0)
        gai_error(rc, "getnameinfo");
    return rc;
}

/* ============================================================
 * 클라이언트/서버 소켓 함수
 * ============================================================ */
int Open_clientfd(const char *hostname, const char *port)
{
    int clientfd = -1;
    struct addrinfo hints, *listp = NULL, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM; /* TCP */
    hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC; /* IPv4/IPv6 */

    /* 실패 시 -1 리턴 (클라이언트는 보통 종료 대신 에러 처리 기회 줌) */
    if (getaddrinfo(hostname, port, &hints, &listp) != 0)
        return -1;

    for (p = listp; p; p = p->ai_next)
    {
        clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (clientfd < 0)
            continue;
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) == 0)
            break; /* success */
        close(clientfd);
        clientfd = -1;
    }
    if (listp)
        freeaddrinfo(listp);
    return clientfd; /* success: fd, fail: -1 */
}

/* 내부 구현(소문자) — 질문에서 주신 버전 기반 */
int open_listenfd(char *port)
{
    struct addrinfo hints, *listp, *p;
    int listenfd, optval = 1;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
    hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */
    Getaddrinfo(NULL, port, &hints, &listp);

    /* Walk the list for one that we can bind to */
    for (p = listp; p; p = p->ai_next)
    {
        /* Create a socket descriptor */
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; /* Socket failed, try the next */

        /* Eliminates "Address already in use" error from bind */
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *)&optval, sizeof(int));
        /* Bind the descriptor to the address */
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
            break;       /* Success */
        Close(listenfd); /* Bind failed, try the next */
    }

    /* Clean up */
    Freeaddrinfo(listp);
    if (!p) /* No address worked */
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
    {
        Close(listenfd);
        return -1;
    }
    return listenfd;
}

/* 교재 스타일 대문자 래퍼 — 실패 시 종료 */
int Open_listenfd(const char *port)
{
    int fd = open_listenfd((char *)port);
    if (fd < 0)
        app_error("Open_listenfd error");
    return fd;
}

/* ============================================================
 * 서버 echo 핸들러 (echoserver.c가 선언 후 호출)
 * ============================================================ */
void echo(int connfd)
{
    rio_t rio;
    char buf[MAXLINE];
    size_t n;

    Rio_readinitb(&rio, connfd);
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
    {
        Rio_writen(connfd, buf, n);
    }
}
