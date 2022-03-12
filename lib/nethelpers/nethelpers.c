#include "nethelpers.h"

int connect_to_socket(char *addr, in_port_t port) {
  int sockfd;
  struct sockaddr_in servaddr;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);

  if (inet_pton(AF_INET, addr, &servaddr.sin_addr) < 0) {
    return -1;
  }

  if (connect(sockfd, (SA *)&servaddr, sizeof(servaddr)) < 0) {
    return -1;
  }
  return sockfd;
}
