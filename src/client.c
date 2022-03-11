#include "../lib/cproto/cproto.h"
#include <arpa/inet.h>
#include <assert.h>
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

void put_in_shard(int socket, char *key, int value);
int *get_from_shard(int socket, char *key);

int main(int argc, char *argv[]) {
  int sockfd;
  struct sockaddr_in servaddr;

  if (strcmp(argv[3], "get") != 0 && strcmp(argv[3], "put") != 0) {
    printf("Unsupported operation (\"%s\" does not match \"get\" or \"put\")",
           argv[3]);
    exit(EXIT_FAILURE);
  }

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
    printf("Error connecting to shard: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (strcmp(argv[3], "put") == 0) {
    put_in_shard(sockfd, argv[4], atoi(argv[5]));
  } else {
    int *value = get_from_shard(sockfd, argv[4]);
    if (value == NULL) {
      printf("No value cached for key \"%s\"\n", argv[4]);
    } else {
      printf("Key \"%s\" has cached value %d\n", argv[4], *value);
      free(value);
    }
  }

  return 0;
}

void put_in_shard(int socket, char *key, int value) {
  uint32_t key_len = strlen(key);
  size_t payload_len = sizeof(key_len) + key_len + sizeof(value);

  uint8_t *payload = malloc(payload_len);
  pack_string_int(key, key_len, value, payload);

  CanaryMsg msg = {
      .type = PutClient2Mstr, .payload_len = payload_len, .payload = payload};

  send_msg(socket, msg);
}

int *get_from_shard(int socket, char *key) {
  CanaryMsg req, resp;

  uint32_t payload_len = strlen(key);
  req = (CanaryMsg){.type = GetClient2Shard,
                    .payload_len = payload_len,
                    .payload = (uint8_t *)key};

  send_msg(socket, req);
  receive_msg(socket, &resp);

  assert(resp.type == GetShard2Client);

  if (resp.payload_len == 0)
    return NULL;

  return (int *)resp.payload;
}
