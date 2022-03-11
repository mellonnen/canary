#include "../lib/cproto/cproto.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define CNFPORT 8080

typedef struct sockaddr SA;

int main(int argc, char *argv[]) {
  int sockfd;
  struct sockaddr_in servaddr;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Error when creating socket: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(CNFPORT);

  if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) < 0) {
    printf("Error converting string IP to bytes: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (connect(sockfd, (SA *)&servaddr, sizeof(servaddr)) < 0) {
    printf("Error connecting to cnf: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  CanaryMsg msg = {.type = ShardInfoClient2Shard,
                   .payload_len = 7,
                   .payload = (uint8_t *)"limpan"};
  send_msg(sockfd, msg);
  return 0;
}
