#include <stdio.h>
#include "csapp.h"

void DupHeader(int connfd, char *buf, char *method, char *uri, char* version);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void Proxy_Http_Traffic(char *hostname, char *port, int connfd);
void make_header(char *header, char *hostname, char *path, rio_t *rio);

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void *thread(void *vargp)
{
    char uri[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char buf[MAXLINE], method[MAXLINE], version[MAXLINE], header[MAXLINE], rio[MAXLINE];
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    DupHeader(connfd, buf, method, uri, version);  
    parse_uri(uri, hostname, port, path);
    // printf("Hostname: %s\n", hostname);
    // printf("Port: %s\n", port);
    // printf("Method: %s\n", method);
    // printf("URI: %s\n", uri);
    // printf("Version: %s\n", version);
    // printf("Path: %s\n", path);
    make_header(header, hostname, path, rio);
    Proxy_Http_Traffic(hostname, port, connfd);
    Close(connfd); // 연결 닫기.

    return NULL;
}

int main(int argc, char **argv) {
    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) { 
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);  

    while (1) { 
        clientlen = sizeof(clientaddr); 
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfd);
        // printf("connfd:%d\n",connfd); 
        // Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        // printf("Accepted connection from (%s, %s)\n", hostname, port);
    }
    return 0;
}

/*
    클라이언트로부터 수신된 HTTP요청의 첫 번째 레더 라인에서 
    메서드, URI, HTTP 버전을 추출.
*/
void DupHeader(int connfd, char *buf, char *method, char *uri, char *version)
{
    rio_t rio; // 구조체 초기화하고

    Rio_readinitb(&rio, connfd); // 클라이언트 소켓에서 데이터 읽을 수 있게 준비.
    Rio_readlineb(&rio, buf, MAXLINE); // 첫 번째 헤더 라인을 읽고 버퍼에 저장
    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version); // 메서드, URI, 버전 추출해서 각각에 저장해준다.
}

/*
    URI 문자열에서 호스티이름, 포트 번호, 경로를 추출하는 파싱 함수.
*/
int parse_uri(char *uri, char *hostname, char *port, char *path)
{
    char *p;
    char arg1[MAXLINE], arg2[MAXLINE];

    if ((p = strchr(uri, '/')) != NULL)
    {
        sscanf(p + 2, "%s", arg1);
    }
    else
    {
        strcpy(arg1, uri);
    }
    if ((p = strchr(arg1, ':')) != NULL)
    {
        *p = '\0';
        sscanf(arg1, "%s", hostname);
        sscanf(p + 1, "%s", arg2);
        p = strchr(arg2, '/');
        *p = '\0';
        sscanf(arg2, "%s", port);  
        *p = '/';  
        sscanf(p, "%s", path);  
    }
    else
    {
        strcpy(hostname, arg1);  
        strcpy(port, "80");  // 기본 포트인 80으로 설정
        strcpy(path, "/");
    }
    return 0;
}

/*
    요청 헤더를 생성하는 함수.
*/
void make_header(char *header, char *hostname, char *path, rio_t *rio)
{
  char buf[MAXLINE], r_host_header[MAXLINE], other_header[MAXLINE];

  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  printf("buf : %s", buf);
}

/*
    클라이언트와 서버 간의 데이터 흐름을 중개한다.
    - 클라이언트는 프록시 서버로 요청을 보낸다.
    - 프록시 서버는 해당 요청을 원격 서버로 전달하고, 다시 원격 서버의 응답을 클라이언트에게 전달한다.
*/
void Proxy_Http_Traffic(char *hostname, char *port, int connfd)
{
  char buf[MAXLINE];
  int clientfd;
  rio_t rio;

  clientfd = Open_clientfd(hostname, port);
  Rio_readinitb(&rio, clientfd);

  sprintf(buf, "%s\r\n", buf); // 빈 줄 추가 (HTTP 요청 헤더의 끝을 표시)

  Rio_writen(clientfd, buf, strlen(buf)); // 함수를 사용해 원격 서버로부터 읽은 데이터를 버퍼에 저장하고

  ssize_t bytesread; // 크기를 변수에 저장한다.

  while((bytesread = Rio_readlineb(&rio, buf, MAXLINE)) > 0){ // 만약 데이터가 없으면 루프 종료.
    Rio_writen(connfd, buf, bytesread); // 읽은 데이터를 쓰고 클라이언트에게 전송
  }
  Close(clientfd);
}