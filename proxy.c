#include <stdio.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*
char *uri: 캐시된 리소스의 URI를 저장하는 포인터다. URI에 대한 캐시를 식별.
char *content: 캐시된 리소스의 내용을 저장하는 포인터. 실제 리소스의 데이터를 가리킴.
int content_length: 캐시된 리소스의 내용의 길이 / 캐시된 데이터의 크기를 나타냄.
struct cache_node *next: 다음 노드를 가리키는 포인터. 이를 통해 캐시 리스트의 다음 노드를 연결.
struct cache_node *prev: 이전 노드를 가리키는 포인터. 이를 통해 캐시 리스트의 이전 노드를 연결.
구조체를 사용하여 각 캐시 노드는 자신의 URI, 콘텐츠 및 길이를 저장하고, 이전 노드와 다음 노드를 가리키는 링크를 통해 리스트를 구성.
*/
typedef struct cache_node
{
    char *uri;
    char *content;
    int content_length;
    struct cache_node *next;
    struct cache_node *prev;
} cache;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int cache_size = 0;
void parse_uri(char *uri, char *host, char *port, char *path);
void doit(int fd);
void *thread(void *vargp);
cache *find_cache(cache *node,char *uri);
void delete_cache(cache *node, cache **head, cache **tail, int *size);
void move_to_front_cache(cache *node, cache *head, cache *tail);
cache *head, *tail;

int main(int argc, char **argv)
{ 
  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

/*
    head와 tail이라는 두 개의 포인터에 cache 구조체 크기만큼 동적 메모리를 할당.
    각각 리스트의 시작과 끝
*/
  head = malloc(sizeof(cache));
  tail = malloc(sizeof(cache));
  head->next = tail;
  tail->prev = head;
  head->prev = NULL;
  tail->next = NULL;
  head->uri = NULL;
  tail->uri = NULL;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* Open_listenfd 함수 호출 -> 듣기 식별자 오픈, 인자를 통해 port번호 넘김 */
  listenfd = Open_listenfd(argv[1]);

  /* 무한 서버 루프 실행 */
  while (1)
  {
    clientlen = sizeof(clientaddr); // accept 함수 인자에 넣기 위한 주소 길이 계산

    /* 반복적 연결 요청 접수 */
    // Accept(듣기 식별자, 소켓 주소 구조체 주소, 해당 주소 길이)
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 쓰레드 경쟁을 피하기 위해 자신만의 동적 메모리 블럭에 할당
    Pthread_create(&tid, NULL, thread, connfd);
    // Getnameinfo => 위의 Getaddrinfo의 반대로, 소켓 주소 구조체 -> 스트링 표시로 변환
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
  }
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self()); // 쓰레드 분리 후 종료시 반환해야 함.
  Free(vargp);                    // 메인 쓰레드가 할당한 메모리 블록 반환
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int fd) {
  int proxyfd, total_size; // 목적지 서버와의 연결을 위한 소켓 디스크립터와 전체 응답 크기
  char host[MAXLINE], port[MAXLINE], path[MAXLINE]; // URI에서 추출한 호스트, 포트, 경로
  char buf_client[MAXLINE], buf_server[MAXLINE]; // 클라이언트와 서버 간의 통신에 사용할 버퍼
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // HTTP 메소드, URI, 프로토콜 버전
  rio_t rio_client, rio_server; // 각각 클라이언트와 서버와의 통신을 위한 rio 구조체
  cache *current_node; // 캐시에서 현재 요청에 해당하는 노드를 가리키는 포인터
  
  char *cache_uri = (char*)Malloc(MAX_OBJECT_SIZE); // 캐시에 저장할 URI. 새로운 요청마다 초기화
  
  // 클라이언트로부터 요청 수신
  Rio_readinitb(&rio_client, fd);
  Rio_readlineb(&rio_client, buf_client, MAXLINE);
  sscanf(buf_client, "%s %s %s", method, uri, version); // 요청으로부터 메소드, URI, 버전 추출
  printf("----client----\n");
  printf("Request headers:\n");
  printf("%s", buf_client); // 클라이언트 요청 로깅
  if (strcasecmp(method, "GET")) {
    // GET 메소드가 아닌 경우 에러 반환
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  strcpy(cache_uri, uri); // 요청받은 URI를 캐시 URI로 저장

  // 캐시에서 요청 URI에 해당하는 콘텐츠 검색
  current_node = find_cache(head->next, cache_uri);

  if (current_node != NULL) {
    // 캐시에 존재하는 경우, 캐시 내용 반환
    move_to_front_cache(current_node, head, tail); // LRU 캐시 정책에 따라 노드 위치 맨 앞으로
    Rio_writen(fd, current_node->content, current_node->content_length); // 캐시된 콘텐츠 클라이언트에게 바로 전송
    return;
  }

  // 목적지 서버의 호스트, 포트, 경로 추출
  parse_uri(uri, host, port, &path);
  proxyfd = Open_clientfd(host, port); // 목적지 서버로 연결
  sprintf(buf_server, "%s %s %s \r\n", method, path, version); // 서버로 보낼 요청 준비

  // 캐시 저장을 위한 버퍼 초기화
  char* buf_cache = (char*)malloc(MAX_OBJECT_SIZE);
  strcpy(buf_cache, "");

  // 목적지 서버와의 통신을 위한 rio 구조체 초기화
  Rio_readinitb(&rio_server, proxyfd);
  // 서버로 전송할 HTTP 헤더 수정
  modify_http_header_for_server(buf_server, host, port, path, &rio_client);
  Rio_writen(proxyfd, buf_server, strlen(buf_server)); // 수정된 요청 서버로 전송

  // 서버로부터 응답을 받아 클라이언트에게 전송하며, 캐시 크기를 초과하지 않는 경우 캐시에 저장
  size_t n, total_n = 0;
  while ((n = Rio_readlineb(&rio_server, buf_server, MAXLINE)) != 0) {
    total_n += n;
    Rio_writen(fd, buf_server, n);
    if (total_n < MAX_OBJECT_SIZE) {
      strcat(buf_cache, buf_server);
    }
  }
  // 목적지 서버와의 연결 종료
  Close(proxyfd);

  // 캐시에 응답 내용 저장
  insert_cache(cache_uri, head, tail, buf_cache, total_n);
}

/*
    캐시 검색(find_cache 함수): 주어진 URI를 가진 콘텐츠가 캐시에 있는지 검색.
    캐시는 이중 연결 리스트로 구현되어 있음.
    이 함수는 캐시 리스트를 순회하며 URI가 일치하는 노드를 찾고, 해당 노드를 반환.
*/
cache *find_cache(cache *node,char *uri){
  
  while (node->uri != NULL) {   // 현재 노드(node)의 uri 멤버가 NULL이 아닐 때까지. 연결 리스트를 끝까지 탐색하기 위한 조건.
    if (strcmp(node->uri,uri) == 0){    // if 문을 사용하여 현재 노드의 uri와 찾고자 하는 uri 문자열이 같은지 비교.
      return node;  // 조건이 참이면 현재 노드(node)를 반환. 이는 찾고자 하는 uri를 가진 노드를 찾았다는 것을 의미.
    }
    node = node->next;  // 현재 노드에서 원하는 uri를 찾지 못한 경우, 다음 노드로 이동. 연결 리스트에서 다음 노드로 이동하도록 함.
  }
  return NULL;  // 주어진 uri를 가진 노드를 찾지 못함.
}

/*
    캐시 삽입(insert_cache 함수): 새로운 콘텐츠를 캐시에 추가함.
    만약 새로운 콘텐츠의 크기가 설정된 최대 콘텐츠 크기(MAX_OBJECT_SIZE)를 초과하면, 캐시에 저장하지 않음. 
    새 콘텐츠를 추가하기 전에 총 캐시 크기가 최대 캐시 크기(MAX_CACHE_SIZE)를 초과하지 않도록 기존 캐시를 삭제.
    새 콘텐츠는 캐시의 맨 앞에 삽입.
*/
void insert_cache(char *uri, cache *head, cache *tail, char *buf, int size){
  // 컨텐츠 사이즈가 캐시 저장 사이즈보다 크면 캐시에 저장안하고 리턴
  if (MAX_OBJECT_SIZE < size)
    return;

  // 새로운 캐시 노드를 위한 메모리 할당
  cache *node = malloc(sizeof(cache));

  // 컨텐츠 사이즈와 기존 나의 캐시 사이즈가 캐쉬의 총 사이즈보다 크거나 같으면 기존에 있는 캐시 제거
  while(size + cache_size >= MAX_CACHE_SIZE){
    delete_cache(node, head, tail, cache_size);
  }
  
  // 새로운 노드에 URI, 컨텐츠 길이 및 컨텐츠 복사
  node->uri = uri; // 노드의 URI 필드에 입력받은 URI 저장
  node->content_length = size; // 노드의 컨텐츠 길이 필드에 입력받은 사이즈 저장
  node->content = buf; // 노드의 컨텐츠 필드에 입력받은 컨텐츠(버퍼) 저장

  // 새로운 노드를 캐시 리스트의 앞부분에 삽입
  node->next = head->next; // 새 노드의 다음 노드를 현재 헤드의 다음 노드로 설정
  head->next->prev = node; // 헤드 다음 노드의 이전 노드를 새 노드로 설정
  head->next = node; // 헤드의 다음 노드를 새 노드로 설정
  node->prev = head; // 새 노드의 이전 노드를 헤드로 설정
}

/*
    캐시 삭제(delete_cache 함수): 캐시에서 특정 노드를 삭제함. 
    캐시에서 가장 오래된 항목(리스트의 끝 부분)을 삭제할 때 사용. 삭제된 노드의 메모리는 해제.
*/
void delete_cache(cache *node, cache **head, cache **tail, int *size)
{
  // 만약 삭제할 노드가 목록의 첫 번째 노드라면
  if (node == *head) {
    *head = node->next; // 헤드를 다음 노드로 업데이트
  }
  // 만약 삭제할 노드가 목록의 마지막 노드라면
  if (node == *tail) {
    *tail = node->prev; // 테일을 이전 노드로 업데이트
  }
  // 이전 노드의 next 포인터를 현재 노드의 다음 노드로 설정 (현재 노드가 첫 번째 노드가 아닐 경우)
  if (node->prev != NULL) {
    node->prev->next = node->next;
  }
  // 다음 노드의 prev 포인터를 현재 노드의 이전 노드로 설정 (현재 노드가 마지막 노드가 아닐 경우)
  if (node->next != NULL) {
    node->next->prev = node->prev;
  }
  
  // 캐시의 전체 크기에서 현재 노드의 내용 길이를 빼서 업데이트
  *size -= node->content_length;
  
  // 현재 노드의 메모리 할당 해제
  free(node);
}

// 캐시 찾아서 맨 앞으로 보내기
void move_to_front_cache(cache *node, cache *head, cache *tail){
  // 맨 앞인 경우에는 아무 작업도 수행하지 않음
  if (node->prev == head)
    return; 
  // 맨 뒤가 아닌 경우, 즉 중간에 위치한 경우
  else if (node->next != tail){
    // 현재 노드의 다음 노드가 현재 노드의 이전 노드를 가리키도록 설정
    node->next->prev = node->prev;
    // 현재 노드의 이전 노드가 현재 노드의 다음 노드를 가리키도록 설정
    node->prev->next = node->next;
  }
  // 맨 뒤에 위치한 경우
  else {
    // 현재 노드의 이전 노드가 tail을 가리키도록 설정
    node->prev->next = tail;
    // tail의 이전 노드가 현재 노드의 이전 노드를 가리키도록 설정
    tail->prev = node->prev;
  }
/*
- node->prev = head;: 
현재 노드의 prev 포인터를 head로 설정.
이는 현재 노드를 리스트의 맨 앞으로 이동시키기 위한 첫 단계, 
현재 노드의 이전 노드를 가리키는 것이 head가 되어야 함.

- node->next = head->next;: 
현재 노드의 next 포인터를 head의 다음 노드로 설정. 
이 작업을 통해 현재 노드는 head 바로 뒤에 오는 노드를 가리키게 됨. 
즉, 현재 노드가 리스트의 첫 번째 실제 데이터 노드가 되게 하는 것.

- head->next = node;: 
마지막으로, head의 next 포인터를 현재 노드로 설정. 
이로써 head 바로 다음에 현재 노드가 위치하게 되며, 
이전에 head 다음에 있던 노드는 이제 현재 노드의 다음 노드가 됨.
이렇게 함으로써, 현재 노드가 리스트의 맨 앞으로 이동하게 됨.
*/
  node->prev = head;
  node->next = head->next;
  head->next = node;
}

// 목적지 서버에 보낼 HTTP 요청 헤더로 수정하기
void modify_http_header_for_server(char *http_header, char *hostname, int port, char *path, rio_t *server_rio)
{
  char buf[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  sprintf(http_header, "GET %s HTTP/1.0\r\n", path);

  while (Rio_readlineb(server_rio, buf, MAXLINE) > 0) // 데이터를 읽어오면 실제 읽어온 데이터 byte만큼 return
  {
    if (strcmp(buf, "\r\n") == 0)
      break;

    if (!strncasecmp(buf, "Host", strlen("Host"))) // buf에서 host있는지 확인
    {
      strcpy(host_hdr, buf); // 있으면 host_hdr에 저장
      continue;
    }
    // connection, proxy-connection, user-agent가 하나라도 맞으면 0 을 반환하므로 if문이 거짓이 됨. 따라서 아닌것들만 other_hdr에 저장
    if (strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) && strncasecmp(buf, "User-Agent", strlen("User-Agent")))
    {
      strcat(other_hdr, buf);
    }
  }
  // host_hdr이 비어있으면 저장
  if (strlen(host_hdr) == 0)
  {
    sprintf(host_hdr, "Host: %s:%d\r\n", hostname, port);
  }
  // 헤더 만들기
  sprintf(http_header, "%s%s%s%s%s%s%s", http_header, host_hdr, "Connection: close\r\n", "Proxy-Connection: close\r\n", user_agent_hdr, other_hdr, "\r\n");
  return;
}

void parse_uri(char *uri, char *host, char *port, char *path)
{
  // http://hostname:port/path 형태
  char *ptr = strstr(uri, "//");
  ptr = ptr != NULL ? ptr + 2 : uri; // http:// 넘기기
  char *host_ptr = ptr;              // host 부분 찾기
  char *port_ptr = strchr(ptr, ':'); // port 부분 찾기
  char *path_ptr = strchr(ptr, '/'); // path 부분 찾기

  // 포트가 있는 경우
  if (port_ptr != NULL && (path_ptr == NULL || port_ptr < path_ptr))
  {
    strncpy(host, host_ptr, port_ptr - host_ptr); // 버퍼, 복사할 문자열, 복사할 길이
    strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
  }
  // 포트가 없는 경우 80 으로 설정
  else
  {
    strcpy(port, "80");
    strncpy(host, host_ptr, path_ptr - host_ptr);
  }
  strcpy(path, path_ptr);
  return;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE];

  /* Print the HTTP response headers */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* Print the HTTP response body */
  sprintf(buf, "<html><title>Tiny Error</title>");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<body bgcolor="
               "ffffff"
               ">\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
  Rio_writen(fd, buf, strlen(buf));
}