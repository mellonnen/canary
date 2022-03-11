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

int connect_to_socket(char *addr, in_port_t port);
int get_shard(int cnf_socket, char *key, char **shard_addr,
              in_port_t *shard_port);
void put_in_shard(int socket, char *key, int value);
int *get_from_shard(int socket, char *key);

int main(int argc, char *argv[]) {

  if (strcmp(argv[3], "get") != 0 && strcmp(argv[3], "put") != 0) {
    printf("Unsupported operation (\"%s\" does not match \"get\" or \"put\")",
           argv[3]);
    exit(EXIT_FAILURE);
  }
  int cnf_socket, shard_socket;
  char *shard_addr;
  in_port_t shard_port;

  if ((cnf_socket = connect_to_socket(argv[1], atoi(argv[2]))) == -1) {
    printf("Error connection to cnf: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  get_shard(cnf_socket, argv[4], &shard_addr, &shard_port);

  if ((shard_socket = connect_to_socket(shard_addr, shard_port)) == -1) {
    printf("Error connection to shard: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (strcmp(argv[3], "put") == 0) {
    put_in_shard(shard_socket, argv[4], atoi(argv[5]));
  } else {
    int *value = get_from_shard(shard_socket, argv[4]);
    if (value == NULL) {
      printf("No value cached for key \"%s\"\n", argv[4]);
    } else {
      printf("Key \"%s\" has cached value %d\n", argv[4], *value);
      free(value);
    }
  }
  return 0;
}

// TODO: refactor functions below out to library

int connect_to_socket(char *addr, in_port_t port) {
  int sockfd;
  struct sockaddr_in servaddr;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    exit(EXIT_FAILURE);
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

int get_shard(int socket, char *key, char **addr, in_port_t *port) {
  CanaryMsg req, resp;

  req = (CanaryMsg){.type = ShardInfoClient2Cnf,
                    .payload_len = strlen(key),
                    .payload = (uint8_t *)key};

  send_msg(socket, req);
  receive_msg(socket, &resp);

  // TODO: remove assert
  assert(resp.type == ShardInfoCnf2Client);

  unpack_string_short(addr, port, resp.payload);
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

  // TODO: remove assert
  assert(resp.type == GetShard2Client);

  if (resp.payload_len == 0)
    return NULL;

  return (int *)resp.payload;
}
