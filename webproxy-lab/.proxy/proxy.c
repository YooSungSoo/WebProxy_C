#include "csapp.h"

/* 과제에서 제시된 UA 한 줄 */
static const char user_agent_hdr[] =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";

/* ---- prototypes ---- */
static void doit(int fd);
static void read_requesthdrs(rio_t *rp, char *host_header, char *other_header);
static void parse_uri(const char *uri, char *hostname, char *port, char *path);
static void reassemble(char *req, const char *path, const char *hostname,
                       const char *port, const char *other_header);
static void forward_response(int servedf, int fd);
static void clienterror(int fd, const char *cause,
                        const char *errnum, const char *shortmsg, const char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN); /* 연결 조기 종료 시 프로세스 종료 방지 */

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        doit(connfd);
        Close(connfd);
    }
}

static void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host_header[MAXLINE], other_header[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char request_buf[MAXBUF * 2]; /* 헤더 여유 */
    rio_t rio;

    Rio_readinitb(&rio, fd);
    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
        return;

    printf("Request headers:\n%s", buf);

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3)
    {
        clienterror(fd, "request line", "400", "Bad Request",
                    "Malformed request line");
        return;
    }
    if (strcasecmp(method, "GET") != 0)
    {
        clienterror(fd, method, "501", "Not Implemented",
                    "This proxy only implements GET");
        return;
    }

    read_requesthdrs(&rio, host_header, other_header);
    parse_uri(uri, hostname, port, path);

    int servedf = Open_clientfd(hostname, port);
    if (servedf < 0)
    {
        clienterror(fd, hostname, "502", "Bad Gateway",
                    "Failed to connect to origin");
        return;
    }

    reassemble(request_buf, path, hostname, port, other_header);
    Rio_writen(servedf, request_buf, strlen(request_buf));
    forward_response(servedf, fd);

    Close(servedf); /* 누수 방지 */
}

static void read_requesthdrs(rio_t *rp, char *host_header, char *other_header)
{
    char buf[MAXLINE];
    host_header[0] = '\0';
    other_header[0] = '\0';

    while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
    {
        if (!strncasecmp(buf, "Host:", 5))
        {
            strncpy(host_header, buf, MAXLINE - 1);
            host_header[MAXLINE - 1] = '\0';
        }
        else if (!strncasecmp(buf, "User-Agent:", 11) ||
                 !strncasecmp(buf, "Connection:", 11) ||
                 !strncasecmp(buf, "Proxy-Connection:", 17))
        {
            /* 프록시가 고정 값으로 덮어쓸 것이므로 무시 */
            continue;
        }
        else
        {
            size_t cur = strlen(other_header), add = strlen(buf);
            if (cur + add < MAXLINE - 1)
            {
                memcpy(other_header + cur, buf, add + 1);
            }
            /* 넘치면 조용히 버림(혹은 잘라내기) */
        }
    }
}

static void parse_uri(const char *uri, char *hostname, char *port, char *path)
{
    const char *u = uri;

    if (!strncasecmp(u, "http://", 7))
        u += 7;

    const char *slash = strchr(u, '/');
    if (slash)
    {
        strcpy(path, slash);
    }
    else
    {
        strcpy(path, "/");
    }

    char hostport[MAXLINE];
    size_t len = (slash ? (size_t)(slash - u) : strlen(u));
    if (len >= sizeof(hostport))
        len = sizeof(hostport) - 1;
    memcpy(hostport, u, len);
    hostport[len] = '\0';

    char *colon = strchr(hostport, ':');
    if (colon)
    {
        *colon = '\0';
        strcpy(hostname, hostport);
        strcpy(port, colon + 1);
    }
    else
    {
        strcpy(hostname, hostport);
        strcpy(port, "80");
    }
}

static void reassemble(char *req, const char *path, const char *hostname,
                       const char *port, const char *other_header)
{
    int n = 0;
    n += snprintf(req + n, MAXBUF * 2 - n, "GET %s HTTP/1.0\r\n", path);

    if (strcmp(port, "80") == 0)
        n += snprintf(req + n, MAXBUF * 2 - n, "Host: %s\r\n", hostname);
    else
        n += snprintf(req + n, MAXBUF * 2 - n, "Host: %s:%s\r\n", hostname, port);

    n += snprintf(req + n, MAXBUF * 2 - n, "%s", user_agent_hdr);
    n += snprintf(req + n, MAXBUF * 2 - n, "Connection: close\r\n");
    n += snprintf(req + n, MAXBUF * 2 - n, "Proxy-Connection: close\r\n");

    n += snprintf(req + n, MAXBUF * 2 - n, "%s", other_header);
    n += snprintf(req + n, MAXBUF * 2 - n, "\r\n"); /* 헤더 종료 */
}

static void forward_response(int servedf, int fd)
{
    rio_t s_rio;
    char buf[MAXBUF];
    ssize_t n;

    Rio_readinitb(&s_rio, servedf);
    while ((n = Rio_readnb(&s_rio, buf, sizeof(buf))) > 0)
    {
        Rio_writen(fd, buf, (size_t)n);
    }
}

static void clienterror(int fd, const char *cause,
                        const char *errnum, const char *shortmsg, const char *longmsg)
{
    char buf[MAXLINE], body[MAXLINE];
    snprintf(body, sizeof(body),
             "<html><title>Tiny Error</title>"
             "<body bgcolor=ffffff>\r\n"
             "%s: %s\r\n"
             "<p>%s: %s\r\n"
             "<hr><em>The Tiny Web server</em>\r\n</body></html>",
             errnum, shortmsg, longmsg, cause);

    snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "Content-length: %zu\r\n\r\n", strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
