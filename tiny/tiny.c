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
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) { // 터미널에 ./tiny 27017 입력하면 argc = 2, argv[0] = tiny, argv[1] = 27017
  /*
  listenfd : 서버 소켓을 나타낸다. 서버 소켓은 클라이언트의 연결 요청을 수신하는 데 사용된다.
            일반적으로 서버는 하나의 소켓을 열고 이를 listen 상태로 설정해 클라이언트의 연결 요청을 기다린다.

  connfd : 클라이언트와의 통신을 위한 연결 소켓을 나타낸다. 
          서버가 accept 함수를 사용해 클라이언트의 연결 요청을 수락하면,
          새로운 소켓의 파일 디스크립터가 connfd에 저장된다. 이후 서버와 클라이언트 간의 모든 통신은 connfd를 통해 이루어진다.
  */
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];  // connection을 위해 hostname 과 port 선언
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {  // 사용자가 포트 번호를 입력하지 않으면 사용법을 알려주고 프로그램을 종료.
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  printf("현재 Port(argv[1]) : %s\n", argv[1]);
  // listen socket open, 인자로 포트 번호를 넘겨준다. 
  // Open_listenfd는 요청받은 준비된 듣기 식별자(lsitenfd)를 리턴한다.
  listenfd = Open_listenfd(argv[1]);  
  while (1) { // 무한루프 서버
    clientlen = sizeof(clientaddr); // accept 함수 인자에 넣기 위한 주소 길이 계산
    // 반복적으로 연결 접수
    // accept 함수 (듣기 식별자, 소켓주소구조체의 주소, 주소의 길이)
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    printf("connfd:%d\n",connfd); //connfd 확인용
    // getnameinfo 함수 : 소켓주소구조체를 호스트 주소, 포트 번호로 변환한다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 트랜잭션 수행 
    Close(connfd);  // 연결 닫기.
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

// 클라이언트의 요청 라인을 확인해 정적, 동적 콘텐츠를 확인하고 돌려줌
void doit(int fd) // fd는 connfd라는게 중요함!
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    // Read request line and headers
    // 요청 라인을 읽고 분석한다
    // 식별자 fd를 rio_t 타입의 읽기 버퍼(rio)와 연결
    // 한 개의 빈 버퍼를 설정하고, 이 버퍼와 한 개의 오픈한 파일 식별자를 연결
    Rio_readinitb(&rio, fd); 
    // 다음 텍스트 줄을 파일 rio에서 읽고, 이를 메모리 위치 buf로 복사하고, 텍스트라인을 NULL로 종료시킴
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s",buf); // 요청된 라인을 printf로 보여줌 (최초 요청라인: GET / HTTP/1.1)
    sscanf(buf, "%s %s %s", method, uri, version);//buf의 내용을 method, uri, version이라는 문자열에 저장함
    if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method,"HEAD") == 0)){ // GET,HEAD 메소드만 지원함(두 문자가 같으면 0)
        // 다른 메소드가 들어올 경우, 에러를 출력하고
        clienterror(fd, method, "501", "Not implemented",
                    "Tiny does not implement this method");
        return; // main으로 return시킴
    }

    read_requesthdrs(&rio); // 요청 라인을 제외한 요청 헤더를 출력함

    // Parse URI from GET request
    // URI를 filename과 CGI argument string으로 parse하고 
    // request가 static인지 dynamic인지 확인하는 flag를 return한다.(1이면 static)
    is_static = parse_uri(uri, filename, cgiargs); // uri 내용 바탕으로 filename, cgiargs 채워짐
    if (stat(filename, &sbuf) < 0){ // disk에 파일이 없다면 filename을 sbuf에 넣는다. 종류,크기등등이 sbuf에 저장됨, 성공시 0 실패시 -1
        clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn't find this file");
        return;
    }

    if (is_static){ // Serve static content
        // S_ISREG -> 파일 종류 확인: 일반(regular) 파일인지 판별
        // 읽기 권한(S_IRUSR)을 가지고 있는지 판별
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
            clienterror(fd, filename, "403", "Forbidden", // 읽기 권한이 없거나 정규파일 아니면
                        "Tiny couldn't read the file");   // 읽을 수 없다.
            return;
        }
        serve_static(fd, filename, sbuf.st_size, method); // fd: connfd 정적(static) 컨텐츠를 클라이언트에게 제공
    }
    else{ // Serve dynamic content
        // 파일이 실행 가능한지 검증
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs, method); // 실행가능하다면 동적(dynamic) 컨텐츠를 클라이언트에게 제공
    }
}

// error 발생 시, client에게 보내기 위한 response (error message)
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  /*
    브라우저 사용자에게 에러를 설명하는 res 본체에 HTML도 함께 보낸다.
    HTML res는 본체에서 컨텐츠의 크기와 타입을 나타내야 하기 때문에,
    HTML 컨텐츠를 한 개의 스트링으로 만들고 그 길이도 잼
    이는 sprintf를 통해 body는 인자에 스택되어 하나의 긴 스트링으로 저장된다.
  */
  
  /* HTTP response body 생성 */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  //
  /* HTTP response 출력 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 버전, 에러번호, 상태메시지
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body)); // body 입력
}

// Tiny는 request header의 정보를 하나도 사용하지 않는다.
// 왜?? : 요청한 파일을 가져오고 클라이언트에게 반환하는 데에만 관심있기 때문.
//        따라서 Tiny는 단순하고 빠른 요청을 위해 요청 라인만 읽고 나머지 헤더 정보는 무시함.
//요청 라인 한줄, 요청 헤더 여러줄 받는데 
//요청 라인은 저장해주고(우리가 tiny에서 필요한 건 이거임), 요청 헤더들은 그냥 출력
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE); //한 줄을 읽어들인다('\n'을 만나면 break되는 식으로)
    //strcmp(str1,str2) 같은 경우 0을 반환 -> 이 경우만 탈출
    // buf가 '\r\n'이 되는 경우는 모든 줄을 읽고 나서 마지막 줄에 도착한 경우이다.
    // 헤더의 마지막 줄은 비어있다

    while(strcmp(buf, "\r\n")){
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf); //한줄 한줄 읽은 것을 출력한다.(최대 MAXLINE읽기가능)
    }
    return;
}

// Tiny는 정적 컨텐츠를 위한 홈 디렉토리가 자신의 현재 디렉토리이고,
// 실행파일의 홈 디렉토리는 /cgi-bin이라고 가정한다.
/*
  해당 요청이 정적 컨텐츠 요청인지 동적 컨텐츠 요청인지 결정해서 파일 경로와 CGI 인자를 추출하는 데 사용.
*/
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;
    // strstr: (대상문자열, 검색할문자열) -> 검색된 문자열(대상 문자열) 뒤에 모든 문자열이 나오게 됨
    // uri에서 "cgi-bin"이라는 문자열이 없으면, static content
    if(!strstr(uri, "cgi-bin")){ // Static content
        strcpy(cgiargs, ""); //cgiargs 인자 string을 지운다.
        strcpy(filename, "."); // 상대 리눅스 경로이름으로 변환 ex) '.'
        strcat(filename, uri); // 상대 리눅스 경로이름으로 변환 ex) '.' + '/index.html'
        if (uri[strlen(uri)-1] == '/') // URI가 '/'문자로 끝난다면
            strcat(filename, "home.html"); // 기본 파일 이름인 home.html을 추가한다. -> 11.10과제 adder.html로 변경
        return 1;
    }
    else{ // Dynamic content (cgi-bin이라는 문자열 존재)
        // 모든 CGI 인자들을 추출한다.
        // index: 첫 번째 인자에서 두번째 인자를 찾는다. 찾으면 문자의 위치 포인터를, 못찾으면 NULL을 반환
        ptr = index(uri, '?');
        if(ptr){
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else // ?없으면 빈칸으로 둘게
            strcpy(cgiargs, "");
        // 나머지 URI 부분을 상대 리눅스 파일이름으로 변환
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
        // cgiargs: 123&123
        // filename: ./cgi-bin/adder
    }
}

/* 지역 파일의 내용을 포함하고 있는 본체를 가진 HTTP 응답을 보내는 함수 */
void serve_static(int fd, char *filename, int filesize, char *method)
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

  // 과제 11.11
  if(strcasecmp(method,"HEAD") == 0) //head메소드면 return해서 header값만 보여주게 하라.
        return;
    // Send response body to client
    // open(열려고 하는 대상 파일의 이름, 파일을 열 때 적용되는 열기 옵션, 파일 열 때의 접근 권한 설명)
    // return 파일 디스크립터
    // O_RDONLY : 읽기 전용으로 파일 열기
    // 즉, filename의 파일을 읽기 전용으로 열어서 식별자를 받아온다.
    srcfd = Open(filename, O_RDONLY, 0);

    // 요청한 파일을 disk에서 가상메모리 영역으로 mapping한다.
    // mmap을 호출하면 파일 srcfd의 첫 번째 filesize 바이트를 
    // 주소 srcp에서 시작하는 사적 읽기-허용 가상메모리 영역으로 mapping
    // 대충(말록이랑 유사한데 값도 복사해준다)
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);

    /* 과제 11. 9 */
    // Mmap대신 malloc 사용 -> 빈칸에 사용해야하므로 빈 공간에 메모리를 읽어야한다.
    srcp = (char*)malloc(filesize);
    Rio_readn(srcfd,srcp,filesize);

    // 파일을 메모리로 매핑한 후에 더 이상 이 식별자는 필요 없으므로 닫기(치명적인 메모리 누수 방지)
    Close(srcfd);
    // 실제로 파일을 client로 전송
    // rio_writen함수는 주소 srcp에서 시작하는 filesize를 클라이언트의 연결 식별자 fd로 복사
    Rio_writen(fd, srcp, filesize);
    // 매핑된 가상메모리 주소를 반환(치명적인 메모리 누수 방지)
    // Munmap(srcp, filesize);
    free(srcp);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
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
  else if (strstr(filename, ".mp4")) // 11.7 숙제 
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if(Fork() == 0){ // 새로운 Child process를 fork한다.
        // 이때 부모 프로세스는 자식의 PID(Process ID)를, 자식 프로세스는 0을 반환받는다.

        // Real server would set all CGI vars here(실제 서버는 여기서 다른 CGI 환경변수도 설정)

        // QUERY_STRING 환경변수를 요청 URI의 CGI 인자들을 넣겠소. 
        // 세 번째 인자는 기존 환경 변수의 유무에 상관없이 값을 변경하겠다면 1, 아니라면 0
        setenv("QUERY_STRING", cgiargs, 1);

        // @과제11번 수정
        // REQUEST_METHOD 환경변수를 요청 URI의 CGI 인자들로 초기화
        setenv("REQUEST_METHOD", method, 1);

        // Dup2함수를 통해 표준 출력을 클라이언트와 연계된 연결 식별자로 재지정 
        //-> CGI 프로그램이 표준 출력으로 쓰는 모든것은 클라이언트로 바로 감(부모프로세스의 간섭 없이)
        Dup2(fd, STDOUT_FILENO); // Redirect stdout to client
        Execve(filename, emptylist, environ); // Run CGI program 실행(adder를 실행)
    } 
    // 자식이 아니면 즉,부모는 자식이 종료되어 정리되는 것을 기다리기 위해 wait 함수에서 블록된다.
    Wait(NULL); // Parent waits for and reaps child
}