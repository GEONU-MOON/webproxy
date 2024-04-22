#include <stdio.h>
#include "csapp.h"

void DupHeader(int connfd, char *buf, char *method, char *uri, char* version);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void SendToServer(char *hostname, char *port, int connfd);
void make_header(char *header, char *hostname, char *path, rio_t *rio);

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {

    int listenfd, connfd;
    char uri[MAXLINE], hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char buf[MAXLINE], method[MAXLINE], version[MAXLINE], header[MAXLINE], rio[MAXLINE];

    if (argc != 2) { 
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);  

    while (1) { 
        clientlen = sizeof(clientaddr); 
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // printf("connfd:%d\n",connfd); 
        // Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        // printf("Accepted connection from (%s, %s)\n", hostname, port);

        DupHeader(connfd, buf, method, uri, version);  

        // parse_uri를 통해 URI를 파싱하여 hostname, port, path를 얻음
        parse_uri(uri, hostname, port, path);
        printf("Hostname: %s\n", hostname);
        printf("Port: %s\n", port);
        printf("Method: %s\n", method);
        printf("URI: %s\n", uri);
        printf("Version: %s\n", version);
        printf("Path: %s\n", path);

        make_header(header, hostname, path, rio);
        SendToServer(hostname, port, connfd);
        // SendToBrowser();

        Close(connfd); // 연결 닫기.
    }
    print("%s", user_agent_hdr);
    return 0;
}

void make_header(char *header, char *hostname, char *path, rio_t *rio)
{
  char buf[MAXLINE], r_host_header[MAXLINE], other_header[MAXLINE];

  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  printf("buf : %s", buf);
  
}

void DupHeader(int connfd, char *buf, char *method, char *uri, char *version)
{
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("%s", buf);

    // Request Header 첫줄에서 method, uri, version을 추출
    sscanf(buf, "%s %s %s", method, uri, version);
}

int parse_uri(char *uri, char *hostname, char *port, char *path)
{
    char *p;
    char arg1[MAXLINE], arg2[MAXLINE];

    // URI에서 첫 번째 '/'를 찾음
    if ((p = strchr(uri, '/')) != NULL)
    {
        // '/' 이후의 문자열을 arg1에 복사
        sscanf(p + 2, "%s", arg1);
    }
    else
    {
        // '/'가 없는 경우 전체 URI를 arg1에 복사
        strcpy(arg1, uri);
    }

    // ':'를 찾음
    if ((p = strchr(arg1, ':')) != NULL)
    {
        // ':' 이전의 문자열을 호스트 이름으로 설정
        *p = '\0';
        sscanf(arg1, "%s", hostname);

        // ':' 이후의 문자열을 arg2에 복사하여 포트 번호로 설정
        sscanf(p + 1, "%s", arg2);

        // arg2에서 '/'를 찾아 포트 번호와 경로를 추출
        p = strchr(arg2, '/');
        *p = '\0';  // 포트 번호를 널 종료하여 추출
        sscanf(arg2, "%s", port);  // 포트 번호를 설정
        *p = '/';  // 원래 상태로 되돌림
        sscanf(p, "%s", path);  // 경로를 설정
    }
    else
    {
        // ':'가 없는 경우 기본 포트인 80으로 설정하고, 호스트 이름과 경로를 추출
        strcpy(hostname, arg1);  // 호스트 이름 설정
        strcpy(port, "80");  // 기본 포트인 80으로 설정
        strcpy(path, "/");
    }

    return 0;
}

void SendToServer(char *hostname, char *port, int connfd)
{
  char buf[MAXLINE];
  int clientfd;
  rio_t rio;

  clientfd = Open_clientfd(hostname, port);
  Rio_readinitb(&rio, clientfd);

  sprintf(buf, "%s\r\n", buf); // 빈 줄 추가 (HTTP 요청 헤더의 끝을 표시)

  Rio_writen(clientfd, buf, strlen(buf));

  ssize_t bytesread;

  while((bytesread = Rio_readlineb(&rio, buf, MAXLINE)) > 0){
    Rio_writen(connfd, buf, bytesread);
  }

  Close(clientfd);
}

// void SendToBrowser(){
    
// }