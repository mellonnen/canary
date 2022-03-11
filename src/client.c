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
  servaddr.sin_port = htons(atoi(argv[2]));

  if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) < 0) {
    printf("Error converting string IP to bytes: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (connect(sockfd, (SA *)&servaddr, sizeof(servaddr)) < 0) {
    printf("Error connecting to cnf: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  char *key = argv[3];
  uint32_t key_len = strlen(key);
  int value = atoi(argv[4]);
  size_t payload_len = sizeof(key_len) + key_len + sizeof(value);

  uint8_t *payload = malloc(payload_len);
  pack_put_payload(key, key_len, value, payload);

  CanaryMsg msg = {
      .type = PutClient2Mstr, .payload_len = payload_len, .payload = payload};

  send_msg(sockfd, msg);
  return 0;
}
