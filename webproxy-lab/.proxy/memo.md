# 1) 무엇을 만들어야 하나 (요구사항 핵심)

* **단일 기능의 HTTP 프록시**: 클라이언트로부터 요청을 받아, 원 서버에 전달하고, 응답을 다시 클라이언트로 전송. 1단계는 **순차(sequential)**, 이후 **병렬(스레드)**, 마지막으로 **메모리 캐시**를 추가합니다.&#x20;
* **대상 메서드**: 최소 **HTTP/1.0 GET** 지원(브라우저는 1.1로 보내도, 프록시는 1.0으로 변환해 전달). 요청 라인은 `GET /path HTTP/1.0` 형식으로 재작성합니다. 요청/응답 라인의 줄바꿈은 `\r\n`, 요청의 끝은 빈 줄 `\r\n`입니다.&#x20;
* **요청 헤더 정책**:

  * `Host:`는 항상 포함(브라우저가 준 게 있으면 그대로 사용).
  * `User-Agent:`는 과제에서 제공된 문자열을 한 줄로 넣어도 됨.
  * `Connection: close`, `Proxy-Connection: close`를 항상 보냄.
  * 그 외 브라우저가 보낸 헤더는 **그대로 전달**.&#x20;
* **포트 처리**: URL에 `:port`가 있으면 그 포트로, 없으면 **기본 80**. 프록시 자체의 **리스닝 포트**는 커맨드라인 인자(예: `./proxy 15213`). 충돌 방지는 `port-for-user.pl` 사용 권장.&#x20;
* **동시성(2단계)**: 새 연결마다 **스레드 생성**(detached). `open_clientfd`, `open_listenfd`는 thread-safe.&#x20;
* **캐시(3단계)**:

  * 전체 캐시 최대: **1 MiB**
  * 개별 객체 최대: **100 KiB**
  * **근사 LRU** 퇴출 정책, 다중 **동시 읽기 허용** / 쓰기는 단일 스레드만. **단일 배타 락만으로 전체 캐시를 감싸는 설계는 불가**(reader 동시성 보장 필요).&#x20;
* **채점**: 기본 동작(40), 동시성(15), 캐시(15) — `driver.sh` 자동 채점 제공(Linux에서 실행).&#x20;
* **견고성**: 장시간 실행 전제, 잘못된 입력/네트워크 예외(EPIPE, ECONNRESET 등)에 **프로세스 종료 없이** 대응, **메모리/FD 누수 금지**, **SIGPIPE 무시**. **RIO**(csapp.c) 사용 권장, stdio 금지. 바이너리 콘텐츠 안전 처리.&#x20;

# 2) 구현 순서(체크리스트)

## A. 순차 프록시(Part I)

1. **리스닝 소켓** 열기 → accept 루프에서 한 번에 한 클라이언트 처리.&#x20;
2. **요청 파싱**

   * 첫 줄에서 **메서드, URI, 버전** 추출. 메서드는 `GET`만 수락(초기).
   * URI에서 **hostname, port(기본 80), path** 분리.&#x20;
3. **요청 헤더 재작성**

   * 요청라인: `GET {path} HTTP/1.0\r\n`
   * `Host:`(있으면 유지/없으면 추가), **고정** `User-Agent:`, `Connection: close`, `Proxy-Connection: close` 추가.
   * 그 외 헤더는 **필터링 없이 전달**(단, 위 4개는 프록시 기준으로 덮어쓰기).&#x20;
4. **원 서버 접속 → 요청 전송 → 응답 수신/중계**

   * 응답 헤더는 특별한 변형 없이 **그대로 전달**.
   * 본문은 **EOF까지** 스트리밍(HTTP/1.0 close 전략).
5. **에러/신호 처리**

   * `signal(SIGPIPE, SIG_IGN)`
   * `rio_*`로 I/O, 예외(EPIPE/ECONNRESET) 복구.&#x20;

## B. 동시성(Part II)

6. **스레드화**

   * accept 후 **detached thread** 생성, 요청 처리 함수 호출.
   * 메인 스레드는 즉시 다음 accept.&#x20;

## C. 캐시(Part III)

7. **캐시 자료구조 설계**

   * 키: **정규화된 URL**(예: `http://host:port/path`).
   * 값: 바이트 배열(≤100KiB), 크기, LRU 메타(예: 리스트 노드/타임스탬프).
   * 용량: 누적 바이트 ≤ **1MiB**(메타데이터는 용량 계산에서 제외).&#x20;
8. **락 설계**

   * **pthread\_rwlock\_t cache\_lock** 1개(혹은 샤딩):

     * 조회/전송 시 **read lock**(다중 스레드 동시 허용)
     * 삽입/퇴출/LRU 업데이트 시 **write lock**
   * “큰 배타락 1개”로 모든 접근을 막는 것은 **불가**(독자 동시 허용 요구). **RW락은 허용**.&#x20;
9. **삽입 정책**

   * 서버 응답을 **동시에 클라이언트로 스트리밍하며** 별도 버퍼에 누적(100KiB 넘으면 캐싱 포기).
   * 전체 용량 초과 시 **LRU 퇴출** 반복.&#x20;
10. **조회 정책**

* 캐시 히트 시 객체를 즉시 **클라이언트로 복사**(읽기 락), LRU갱신은 짧게 write lock으로 승격하여 처리(또는 lazy timestamp).

# 3) 권장 코드 구조(스켈레톤)

```c
int main(int argc, char** argv) {
  int listenfd = open_listenfd(argv[1]);
  signal(SIGPIPE, SIG_IGN);
  cache_init(); // init rwlock, list/map
  while (1) {
    int connfd = accept(listenfd, ...);
    pthread_create_detached(handle_client, (void*)(long)connfd);
  }
}

void* handle_client(void* arg) {
  int connfd = (int)(long)arg;
  rio_t rio; Rio_readinitb(&rio, connfd);

  // 1) parse request line + headers
  request_t req = parse_request(&rio); // method, uri -> host, port, path

  // 2) try cache
  if (cache_lookup(req.url, &obj)) {
    // read lock inside; write lock for LRU touch
    write_all(connfd, obj.data, obj.len);
    close(connfd); return NULL;
  }

  // 3) connect to server and forward
  int clientfd = open_clientfd(req.host, req.port);
  build_forward_headers(&req, buf); // adds Host/User-Agent/Connection/Proxy-Connection
  rio_writen(clientfd, buf, strlen(buf));

  // 4) stream response + conditional buffer (≤100KiB) for caching
  size_t n, total=0; char cachebuf[MAX_OBJECT_SIZE+1];
  while ((n = rio_readnb(&s_rio, tmp, sizeof(tmp))) > 0) {
    rio_writen(connfd, tmp, n);
    if (total + n <= MAX_OBJECT_SIZE) { memcpy(cachebuf+total, tmp, n); total += n; }
  }

  // 5) insert to cache if total ≤ 100KiB
  if (total <= MAX_OBJECT_SIZE) cache_insert(req.url, cachebuf, total);

  close(clientfd); close(connfd); return NULL;
}
```

(실코드는 에러체크/헤더파싱/CRLF 처리/응답 헤더 경계 처리 등 보완 필요)

# 4) 테스트 전략(도구 & 명령)

* **Autograder**: `./driver.sh` (Linux) — 세 파트 점수 확인.&#x20;
* **Tiny 서버**: 핸드아웃에 포함(채점기도 Tiny 사용). 로컬에서 Tiny와 프록시를 서로 다른 포트로 띄운 뒤 프록시 경유 요청.&#x20;
* **curl**(강력 추천):

  ```bash
  curl -v --proxy http://localhost:15214 http://localhost:15213/home.html
  ```

  프록시와 Tiny가 로컬에서 각각 `15214`, `15213`일 때.&#x20;
* **telnet / netcat(nc)**: 수동으로 요청 전송·검증, 혹은 nc를 임시 서버로 띄워 **프록시가 보낸 진짜 요청**을 관찰.&#x20;
* **브라우저(Firefox)**: 프록시 설정 후 실제 웹 탐색(캐시 테스트 시 **브라우저 캐시 비활성화** 필수).&#x20;

# 5) 주의할 함정(실패 포인트)

* **CRLF 누락**: 요청/헤더 종료 `\r\n\r\n` 정확히.&#x20;
* **헤더 중복/누락**: `Host/User-Agent/Connection/Proxy-Connection` 규칙대로 덮어쓰기/추가.&#x20;
* **HTTP/1.1 그대로 전달**: 반드시 **1.0으로 다운그레이드**.&#x20;
* **chunked 대응**: 1.0 close 전략이면 일반적으로 EOF까지 읽기. `Content-Length`가 없을 수 있으므로 **EOF 스트리밍** 지원.
* **바이너리 안전성**: `rio_readnb/rio_writen`로 바이트 단위 처리(문자열 가정 금지).&#x20;
* **SIGPIPE/ECONNRESET로 종료**: `SIGPIPE` 무시, 에러는 연결 단위로만 정리.&#x20;
* **락 설계 위반**: **reader 동시성** 미보장(큰 mutex 하나)은 감점. **rwlock** 또는 샤딩으로 해결.&#x20;
* **캐시 용량 계산**: **객체 바이트만** 합산(메타 제외). 삽입 전 퇴출로 1MiB 유지.&#x20;

# 6) 바로 시작용 “오늘의 작업” 체크리스트

1. `signal(SIGPIPE, SIG_IGN)` 추가 + RIO 초기 세팅.&#x20;
2. `parse_uri()` 작성(스킴 제거 → host / port / path 분리).
3. 요청 재작성기(`build_forward_headers`) 작성: 위 4개 헤더 규칙 반영.&#x20;
4. 순차 버전 완성 → `curl -v --proxy ...`로 검증.&#x20;
5. 스레드화(detached) → 경합 없이 동작 확인.&#x20;
6. 캐시 골격: `pthread_rwlock_t`, LRU 리스트, 맵(단순히 리스트 선형탐색부터 시작해도 OK) → 히트/미스/삽입/퇴출 동작 검증.&#x20;
7. `driver.sh`로 자가 채점.&#x20;