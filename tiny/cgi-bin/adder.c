/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char  *buf, *p;
  char  arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  /* Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0';
    strcpy(arg1, p - 1);
    strcpy(arg2, p + 3);
    n1 = atoi(arg1);
    n2 = atoi(arg2);
  }

  /* make the response body */
  sprintf(content, "QUERY_STRING = %s", buf);
  sprintf(content, "Welcome to add.com : ");
  sprintf(content, "%s The Internet addition portal. \r\n<p>", content);
  sprintf(content, "%s The answer is : %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  /* Generate the HTTP response */
  printf("Connection : close\r\n");
  printf("Content-length : %d\r\n", (int)strlen(content));
  printf("Content-type : text/html\r\n\r\n");
  if(!strcasecmp(getenv("REQUEST_METHOD"), "HEAD")) {
    fflush(stdout);
    exit(0);
  }
  printf("%s", content);
  fflush(stdout);

  exit(0);
}
/* $end adder */