/*
 * proxy.c — CS:APP Proxy Lab (Part I: Sequential)
 *
 * ✅ 무슨 프로그램?
 *   - 간단한 HTTP 프록시.
 *   - 클라이언트(브라우저/curl)로부터 요청을 받아 원서버(Tiny 등)에 전달하고,
 *     원서버 응답을 다시 클라이언트에게 중계한다.
 *
 * ✅ 이 버전이 지키는 규칙 (핸드아웃 요구사항)
 *   - 요청 라인을 반드시 "HTTP/1.0" 으로 다운그레이드하여 원서버에 보낸다.
 *   - 다음 4개 헤더는 프록시가 책임지고 재작성한다:
 *       Host:
 *       User-Agent:  (과제에서 제시된 한 줄 그대로)
 *       Connection: close
 *       Proxy-Connection: close
 *     → 브라우저가 보낸 동일 키 헤더는 무시하고, 프록시 값으로 덮어쓴다.
 *   - 그 외 헤더는 그대로 전달(필요 시 필터링 가능하지만, 기본은 그대로 pass-through).
 *   - 응답은 특별히 손대지 않고 **EOF까지 바이트 스트림**으로 중계한다
 *     (HTTP/1.0 close 전략. chunked/Content-Length 여부와 무관).
 *

 * ⚠️ 자주 틀리는 포인트
 *   - 헤더 종료의 CRLF 빈 줄: "\r\n" 한 줄이 반드시 있어야 한다.
 *   - Host 헤더에 포트가 80이 아니면 "Host: host:port".
 *   - 요청 라인은 반드시 HTTP/1.0.
 *   - SIGPIPE 무시(클라이언트가 중간에 끊어도 프로세스가 죽지 않게).
 */

#include <stdio.h>

/* (Part III에서 사용할 상수 — 현재 파일에서는 사용하지 않지만, 핸드아웃 권장) */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 과제에서 제공하는 User-Agent 한 줄 (꼭 그대로, 줄 끝 \r\n 포함) */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

#include "csapp.h"

/* ---- 프로토타입(정적 내부 함수) ----
 * 외부 노출을 막고 파일 내부에서만 사용할 함수들은 static으로 선언합니다.
 */
static void doit(int fd);
static void read_requesthdrs(rio_t *rp, char *host_header, char *other_header);
static void parse_uri(const char *uri, char *hostname, char *port, char *path);
static void reassemble(char *req, const char *path, const char *hostname,
                       const char *port, const char *other_header);
static void forward_response(int servedf, int fd);
static void clienterror(int fd, const char *cause,
                        const char *errnum, const char *shortmsg, const char *longmsg);
static void *thread(void *vargp);

/* === 교체된 main === */
int main(int argc, char **argv)
{
  int listenfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char hostname[MAXLINE], port[MAXLINE];

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 클라이언트가 중간에 끊어도 프로세스가 죽지 않도록 */
  Signal(SIGPIPE, SIG_IGN);

  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    clientlen = sizeof(clientaddr);

    /* 연결 수락 */
    int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* 로깅(선택) */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    /* 스레드에 안전하게 넘기기 위해 동적 메모리에 복사 */
    int *connfdp = Malloc(sizeof(int));
    *connfdp = connfd;

    pthread_t tid;
    Pthread_create(&tid, NULL, thread, connfdp);
    /* 부모는 connfd를 닫지 않습니다. thread()가 doit(connfd) 후 Close(connfd) 합니다. */
  }
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  free(vargp);
  pthread_detach(pthread_self());

  doit(connfd);
  Close(connfd);
  return NULL;
}

/*
 * doit(fd)
 *  - 단일 클라이언트 연결을 처리합니다.
 *  - 요청 라인을 읽어 메서드/URI/버전을 파싱하고, 헤더를 읽어
 *    재작성 대상 헤더를 제외한 나머지를 수집합니다.
 *  - URI에서 host/port/path를 뽑아 원서버에 연결한 뒤,
 *    HTTP/1.0 규칙에 맞춘 새로운 요청을 만들어 전송합니다.
 *  - 원서버 응답을 EOF까지 그대로 클라이언트에 중계합니다.
 */
static void doit(int fd)
{
  /* 입력 버퍼 및 파싱용 버퍼들 */
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host_header[MAXLINE], other_header[MAXLINE];     /* Host 한 줄과 그 외 헤더 모음 */
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE]; /* URI 분해 결과 */
  char request_buf[MAXBUF * 2];                         /* 원서버로 보낼 최종 요청 헤더(여유롭게 2*MAXBUF) */
  rio_t rio;                                            /* Robust I/O(라인/바이트 단위 안전 I/O) */

  /* 1) 요청 라인 읽기 */
  Rio_readinitb(&rio, fd);
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
    return; /* EOF — 클라이언트가 연결만 열고 바로 끊었을 수 있음 */

  printf("Request headers:\n%s", buf); /* 디버깅용 */

  /* "METHOD URI VERSION" 파싱 (예: "GET http://h/p HTTP/1.1") */
  if (sscanf(buf, "%s %s %s", method, uri, version) != 3)
  {
    clienterror(fd, "request line", "400", "Bad Request",
                "Malformed request line");
    return;
  }

  /* 이 버전은 GET만 지원 (핸드아웃 Part I 기본) */
  if (strcasecmp(method, "GET") != 0)
  {
    clienterror(fd, method, "501", "Not Implemented",
                "This proxy only implements GET");
    return;
  }

  /* 2) 헤더 읽기 — 프록시가 덮어쓸 4개(User-Agent/Connection/Proxy-Connection/Host) 제외하고 수집 */
  read_requesthdrs(&rio, host_header, other_header);

  /* 3) URI 파싱 — "http://host[:port]/path" 에서 host/port/path 추출
   *    - 포트가 없으면 기본 80
   *    - path가 없으면 "/"
   */
  parse_uri(uri, hostname, port, path);

  /* 4) 원서버 연결 */
  int servedf = Open_clientfd(hostname, port);
  if (servedf < 0)
  {
    /* 원서버 접속 실패 → 502 반환 */
    clienterror(fd, hostname, "502", "Bad Gateway",
                "Failed to connect to origin");
    return;
  }

  /* 5) 원서버로 보낼 요청 헤더 재작성/조립
   *   - 요청 라인: "GET <path> HTTP/1.0\r\n"
   *   - Host: (포트가 80이 아니면 "host:port")
   *   - User-Agent: (과제 지정 문자열)
   *   - Connection: close
   *   - Proxy-Connection: close
   *   - 기타 헤더(other_header): 원본에서 수집한 것을 그대로 이어붙임
   *   - 마지막에 빈 줄("\r\n")
   */
  reassemble(request_buf, path, hostname, port, other_header);

  /* 6) 원서버로 요청 전송 */
  Rio_writen(servedf, request_buf, strlen(request_buf));

  /* 7) 원서버 응답을 EOF까지 그대로 클라이언트에 중계
   *    - HTTP/1.0 close 전략: Content-Length 유무/Transfer-Encoding 상관없이
   *      소켓이 닫힐 때까지 바이트 스트리밍
   */
  forward_response(servedf, fd);

  /* 8) 원서버 소켓 정리 (FD 누수 방지) */
  Close(servedf);
}

/*
 * read_requesthdrs(rp, host_header, other_header)
 *  - 클라이언트가 보낸 요청 헤더를 한 줄씩 읽는다.
 *  - Proxy가 덮어쓸 헤더들(User-Agent/Connection/Proxy-Connection/Host)은
 *    여기서 무시하거나 따로 저장하고, 나머지 헤더는 other_header 버퍼에 누적.
 *  - 헤더 종료("\r\n")를 만나면 리턴.
 *
 * ⚠️ 버퍼 안전성
 *  - other_header는 MAXLINE 크기이므로, 헤더가 아주 많을 경우 일부는 잘릴 수 있음.
 *    (과제 기본 테스트에는 충분. 더 안전히 하려면 동적 버퍼를 쓸 수 있음)
 */
static void read_requesthdrs(rio_t *rp, char *host_header, char *other_header)
{
  char buf[MAXLINE];
  host_header[0] = '\0';
  other_header[0] = '\0';

  while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n"))
  {
    if (!strncasecmp(buf, "Host:", 5))
    {
      /* Host 헤더는 유지(없으면 나중에 추가), 여기선 가장 최근 걸 보관 */
      strncpy(host_header, buf, MAXLINE - 1);
      host_header[MAXLINE - 1] = '\0';
    }
    else if (!strncasecmp(buf, "User-Agent:", 11) ||
             !strncasecmp(buf, "Connection:", 11) ||
             !strncasecmp(buf, "Proxy-Connection:", 17))
    {
      /* 이 3개는 프록시가 고정 값으로 덮어쓸 예정이므로 무시 */
      continue;
    }
    else
    {
      /* 나머지 헤더는 그대로 other_header에 이어붙임 */
      size_t cur = strlen(other_header), add = strlen(buf);
      if (cur + add < MAXLINE - 1)
      {
        memcpy(other_header + cur, buf, add + 1); /* '\0' 포함 복사 */
      }
      /* 넘치면 조용히 버림(실무라면 동적 확장/경고 로깅 권장) */
    }
  }
}

/*
 * parse_uri(uri, hostname, port, path)
 *  - URI가 "http://host[:port]/path" 형태라고 가정하고 분해한다.
 *  - 스킴(http://)은 있으면 건너뛰고, 첫 '/' 전까지를 host[:port], 이후를 path로 본다.
 *  - 포트가 없으면 "80", path가 없으면 "/".
 *
 * 예)
 *  - http://example.com/index.html  → host=example.com, port=80,  path=/index.html
 *  - http://example.com:8080/a/b    → host=example.com, port=8080, path=/a/b
 *  - http://example.com             → host=example.com, port=80,  path=/
 *
 * ⚠️ 프록시 프런트엔드(브라우저)는 보통 "절대URI"를 보냅니다.
 */
static void parse_uri(const char *uri, char *hostname, char *port, char *path)
{
  const char *u = uri;

  /* 스킴 스킵 */
  if (!strncasecmp(u, "http://", 7))
    u += 7;

  /* 첫 슬래시로 path 경계 결정 */
  const char *slash = strchr(u, '/');
  if (slash)
  {
    strcpy(path, slash);
  }
  else
  {
    strcpy(path, "/");
  }

  /* host[:port] 추출 */
  char hostport[MAXLINE];
  size_t len = (slash ? (size_t)(slash - u) : strlen(u));
  if (len >= sizeof(hostport))
    len = sizeof(hostport) - 1;
  memcpy(hostport, u, len);
  hostport[len] = '\0';

  /* 포트가 명시됐는지 체크 */
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

/*
 * reassemble(req, path, hostname, port, other_header)
 *  - 원서버로 보낼 최종 요청 헤더를 조립한다.
 *  - 요청 라인: "GET <path> HTTP/1.0\r\n"
 *  - Host: (포트가 80이 아니면 host:port)
 *  - User-Agent: (과제 지정 UA 그대로)
 *  - Connection: close
 *  - Proxy-Connection: close
 *  - 나머지(other_header) 이어붙인 후, 마지막에 \r\n 한 줄(헤더 종료)
 *
 * ⚠️ CRLF
 *  - 각 헤더는 \r\n 로 끝나야 하고, 마지막에는 빈 줄(\r\n)이 필요합니다.
 */
static void reassemble(char *req, const char *path, const char *hostname,
                       const char *port, const char *other_header)
{
  int n = 0;

  /* 요청 라인: HTTP/1.0 다운그레이드 */
  n += snprintf(req + n, MAXBUF * 2 - n, "GET %s HTTP/1.0\r\n", path);

  /* Host 헤더: 80이 아니면 host:port */
  if (strcmp(port, "80") == 0)
    n += snprintf(req + n, MAXBUF * 2 - n, "Host: %s\r\n", hostname);
  else
    n += snprintf(req + n, MAXBUF * 2 - n, "Host: %s:%s\r\n", hostname, port);

  /* 지정된 User-Agent 고정 */
  n += snprintf(req + n, MAXBUF * 2 - n, "%s", user_agent_hdr);

  /* 프록시/서버와의 연결을 명시적으로 종료(HTTP/1.0 close 모델) */
  n += snprintf(req + n, MAXBUF * 2 - n, "Connection: close\r\n");
  n += snprintf(req + n, MAXBUF * 2 - n, "Proxy-Connection: close\r\n");

  /* 기타 헤더(원본에서 가져온 것)를 그대로 붙임 */
  n += snprintf(req + n, MAXBUF * 2 - n, "%s", other_header);

  /* 헤더 종료 — 빈 줄 */
  n += snprintf(req + n, MAXBUF * 2 - n, "\r\n");
}

/*
 * forward_response(servedf, fd)
 *  - 원서버(servedf)의 응답을 EOF까지 읽어 클라이언트(fd)에 그대로 중계한다.
 *  - RIO의 바이트 단위 읽기(Rio_readnb)로 바이너리 컨텐츠도 안전하게 처리 가능.
 *  - chunked 인코딩/Content-Length 여부와 관계없이 소켓 닫힐 때까지 전송.
 */
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

/*
 * clienterror(fd, cause, errnum, shortmsg, longmsg)
 *  - 간단한 HTML 에러 페이지를 만들어 클라이언트에 보낸다.
 *  - errnum/shortmsg는 상태줄(HTTP/1.0 <num> <msg>)에 사용.
 *  - body에는 원인(cause)와 메시지(longmsg)를 함께 표기.
 *
 * 사용 예)
 *  - 잘못된 메서드: 501 Not Implemented
 *  - 원서버 연결 실패: 502 Bad Gateway
 */
static void clienterror(int fd, const char *cause,
                        const char *errnum, const char *shortmsg, const char *longmsg)
{
  char buf[MAXLINE], body[MAXLINE];

  /* 아주 작은 HTML 본문(가독성용) */
  snprintf(body, sizeof(body),
           "<html><title>Tiny Error</title>"
           "<body bgcolor=ffffff>\r\n"
           "%s: %s\r\n"
           "<p>%s: %s\r\n"
           "<hr><em>The Tiny Web server</em>\r\n</body></html>",
           errnum, shortmsg, longmsg, cause);

  /* 상태줄 + 기본 헤더(타입/길이) + 빈 줄 + 본문 */
  snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  snprintf(buf, sizeof(buf), "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  snprintf(buf, sizeof(buf), "Content-length: %zu\r\n\r\n", strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* =========================
 * 확장 아이디어 / TODO
 * =========================
 * [Part II: 동시성]
 *  - main()에서 Accept 후 connfd마다 스레드를 생성하여 doit(connfd) 호출
 *  - detached thread(pthread_detach)로 조인 없이 수거
 *  - 공유 자원이 생기면 적절한 락 보호 필요
 *
 * [Part III: 캐시]
 *  - 키: 정규화된 URL (http://host:port/path) — parse_uri 결과로 구성
 *  - 값: 응답 객체(≤100KiB), 총합 ≤1MiB
 *  - 정책: (근사)LRU, 다중 읽기 동시 허용 — pthread_rwlock_t 권장
 *  - 구현 팁:
 *      • forward_response에서 클라이언트로 바로 쓰면서, 최대 100KiB까지 별도 버퍼에 백업
 *      • 전송 끝나면 버퍼를 캐시에 삽입(한 번의 write lock), LRU는 touch/timestamp
 *      • 히트 시 read lock으로 바로 복사, LRU 업데이트는 짧게 write lock
 */
