/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */

 /*
 -  make tiny
 -  ./tiny 27017
 -  http://52.78.180.6:27017/
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  /*
  listenfd : 서버 소켓을 나타낸다. 서버 소켓은 클라이언트의 연결 요청을 수신하는 데 사용된다.
            일반적으로 서버는 하나의 소켓을 열고 이를 listen 상태로 설정해 클라이언트의 연결 요청을 기다린다.

  connfd : 클라이언트와의 통신을 위한 연결 소켓을 나타낸다. 
          서버가 accept 함수를 사용해 클라이언트의 연결 요청을 수락하면,
          새로운 소켓의 파일 디스크립터가 connfd에 저장된다. 이후 서버와 클라이언트 간의 모든 통신은 connfd를 통해 이루어진다.
  */
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];  // 크기를 모르기 때문에 맥스로 받음.
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {  // 사용자가 포트 번호를 입력하지 않으면 사용법을 알려주고 프로그램을 종료.
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);  // open_listenfd함수를 호출해서 지정된 포트로 소켓을 연다. 
                                      // 이를 통해 클라이언트의 연결 요청을 받을 준비를 함.
  while (1) { // 무한루프 시작. 서버가 클라이언트의 연결 요청 받기를 항상 기다림.
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // 반복적으로 연결 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 트랜잭션 수행 
    Close(connfd);  // 자신쪽의 연결 끝을 닫는다.
  }
}

/* doit에서 쓰이는 stat struct */
// struct stat{
//   dev_t st_dev; /* ID of device containing file */
//   ino_t st_ino; /* inode number */
//   mode_t st_mode; /* 파일의 종류 및 접근권한 */
//   nlink_t st_nlink; /* hardlink 된 횟수 */
//   uid_t st_uid; /* 파일의 owner */
//   gid_t st_gid; /* group ID of owner */
//   dev_t st_rdev; /* device ID (if special file) */
//   off_t st_size; /* 파일의 크기(bytes) */
//   blksize_t st_blksize; /* blocksize for file system I/O */
//   blkcnt_t st_blocks; /* number of 512B blocks allocated */
//   time_t st_atime; /* time of last access */
//   time_t st_mtime; /* time of last modification */
//   time_t st_ctime; /* time of last status change */ 
// }

void doit(int fd) // 한 개의 HTTP 트랜잭션을 처리
{
  
  int is_static; // 정적파일인지 아닌지를 판단해주기 위한 변수
  struct stat sbuf; // 파일에 대한 정보를 가지고 있는 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd); // 요청 라인 읽기
  Rio_readlineb(&rio, buf, MAXLINE); // 요청 라인 읽고 분석
  printf("Request headers:\n");
  printf("%s", buf); // 우리가 요청한 자료 표준 출력.
  sscanf(buf, "%s %s %s", method, uri, version); // buf에 있는 데이터를 method, uri, version에 담기
  printf("Get image file uri : $s\n", uri); // 추가 코드
  /* 숙제 11.11 */
  if (strcasecmp(method, "GET")) { // method가 GET이 아니라면 error message 출력
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); // GET method 라면 읽고, 다른 요청 헤더들은 무시

  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
      clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
      return;
  }
  
  if (is_static) {  /* Serve static content */ // request file이 static contents이면 실행
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // file이 정규파일이 아니거나 사용자 읽기가 안되면 error message 출력
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); // response static file
  }
  else {  /* Serve dynamic content */ // request file이 dynamic contents이면 실행
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // file이 정규파일이 아니거나 사용자 읽기가 안되면 error message 출력
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // response dynamic files
  }
}

// error 발생 시, client에게 보내기 위한 response (error message)
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // response body 쓰기 (HTML 형식)
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  //response 쓰기
  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 버전, 에러번호, 상태메시지
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body)); // body 입력
}

/* HTTP 요청 헤더를 읽고 출력하는 함수 */
void read_requesthdrs(rio_t *rp)  // HTTP 요청 헤더를 읽는다.
{
  char buf[MAXLINE]; // 요청 헤더를 읽을 버퍼

  Rio_readlineb(rp, buf, MAXLINE);  // 한 줄씩 요청을 읽어서 buf에 저장, 최대 MAXLINE 바이트까지 읽는다. 
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);  // 읽은 요청 헤더를 출력.
  }
  return;
}

// uri parsing을 하여 static을 request하면 0, dynamic을 request하면 1반환
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) {  /* Static content */ // parsing 결과, static file request인 경우 (uri에 cgi-bin이 포함이 되어 있지 않으면)
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/') { // request에서 특정 static contents를 요구하지 않은 경우 (home page 반환)
      strcat(filename, "home.html");
    }
    return 1;
  }
  else {  /* Dynamic content */ // parsing 결과, dynamic file request인 경우
    ptr = index(uri, '?');  // uri부분에서 file name과 args를 구분하는 ?위치 찾기
    if (ptr) {  // ?가 있으면 
      strcpy(cgiargs, ptr+1); //cgiargs에 인자 넣어주기
      *ptr = '\0'; // 포인터 ptr은 null처리
    }
    else { // ?가 없으면
      strcpy(cgiargs, "");
    }
    // filename에 uri담기
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/* 지역 파일의 내용을 포함하고 있는 본체를 가진 HTTP 응답을 보내는 함수 */
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype); // 파일 이름의 접미어 검사해서 파일 타입 결정
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  // 클라이언트에 응답 줄과 응답 헤더를 보낸다.
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);  // 빈 줄 한 개가 헤더를 종료한다.
  Rio_writen(fd, buf, strlen(buf)); // 요청한 파일의 내용을 연결 식별자로 복사해서 응답 본체를 보낸다.
  /* 서버쪽에 출력 */
  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

  /* 
  * get_filetype - Derive file type from filename
  */
  void get_filetype(char *filename, char *filetype)
  {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* Child */
  /* Real server would set all CGI vars here */
  setenv("QUERY_STRING", cgiargs, 1);
  Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
  Execve(filename, emptylist, environ); /* Run CGI program */
  }
  wait(NULL); /* Parent waits for and reaps child */
}