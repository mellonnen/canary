#include "../lib/cproto/cproto.h"
#include "../lib/lru//lru.h"
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
#define BACKLOG 100

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;
typedef struct in_addr IA;

lru_cache_t *cache;

int register_with_cnf(char *cnf_ip, in_port_t shard_port);
int run(in_port_t shard_port);
void handle_connection(int client_socket, IA client_addr);
void handle_put(uint8_t *payload);

int main(int argc, char *argv[]) {
  if (argc < 4) {
    printf("Usage: ./shard <ip-of-cnf-service> <port-for-shard> <cache-size>");
    exit(EXIT_FAILURE);
  }
  in_port_t port = atoi(argv[2]);
  if (register_with_cnf(argv[1], port) == -1)
    exit(EXIT_FAILURE);

  cache = create_lru_cache(atoi(argv[3]));
  if (run(port) == -1)
    exit(EXIT_FAILURE);
}

int register_with_cnf(char *cnf_ip, in_port_t shard_port) {
  int sockfd;
  struct sockaddr_in servaddr;

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Error when creating socket: %s\n", strerror(errno));
    return -1;
  }

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(CNFPORT);

  if (inet_pton(AF_INET, cnf_ip, &servaddr.sin_addr) < 0) {
    printf("Error converting string IP to bytes: %s\n", strerror(errno));
    return -1;
  }

  if (connect(sockfd, (SA *)&servaddr, sizeof(servaddr)) < 0) {
    printf("Error connecting to cnf: %s\n", strerror(errno));
    return -1;
  }
  uint8_t payload[2];
  pack_register_payload(shard_port, payload);

  send_msg(sockfd, (CanaryMsg){.type = RegisterShard2Cnf,
                               .payload_len = 2,
                               .payload = payload});
  CanaryMsg msg;

  receive_msg(sockfd, &msg);

  switch (msg.type) {
  case RegisterCnf2Mstr:
    printf("Successfully registered shard.\n");
    return 0;
  case Error:
    printf("Failed to register shard: %s\n", msg.payload);
    return -1;
  default:
    printf("Received wrong message type %d\n", msg.type);
    return -1;
  }
}

int run(in_port_t shard_port) {
  int shard_socket, client_socket, addr_size;
  SA_IN shard_addr, client_addr;

  if ((shard_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Error creating socket: %s\n", strerror(errno));
    return -1;
  }

  shard_addr.sin_family = AF_INET;
  shard_addr.sin_addr.s_addr = INADDR_ANY;
  shard_addr.sin_port = htons(shard_port);

  if ((bind(shard_socket, (SA *)&shard_addr, sizeof(shard_addr))) < 0) {
    printf("Error binding socket: %s\n", strerror(errno));
    return -1;
  }
  if ((listen(shard_socket, BACKLOG)) == -1) {
    printf("Error during listen\n");
    exit(EXIT_FAILURE);
  }

  while (1) {
    printf("Waiting for connections...\n\n");
    addr_size = sizeof(SA_IN);

    if ((client_socket = accept(shard_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      printf("Accept failed\n\n");
      continue;
    }

    printf("Made connection with %s:%d\n\n", inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    handle_connection(client_socket, client_addr.sin_addr);
  }
}

void handle_connection(int socket, IA client_addr) {
  CanaryMsg msg;

  if (receive_msg(socket, &msg) == -1) {
    send_error_msg(socket, "Could not receive message");
    return;
  }

  switch (msg.type) {
  case PutClient2Mstr:
    handle_put(msg.payload);
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
}

void handle_put(uint8_t *payload) {
  char *key;
  int value;

  unpack_put_payload(&key, &value, payload);
  lru_entry_t *removed = put(cache, key, value);

  printf("Put key value pair (%s, %d) ", key, value);
  if (removed != NULL) {
    printf("and expelled key value pair (%s, %d) from cache\n", removed->key,
           removed->value);
  } else {
    printf("in cache\n");
  }
}
