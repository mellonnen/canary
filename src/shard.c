#include "../lib/cproto/cproto.h"
#include "../lib/lru//lru.h"
#include "../lib/nethelpers/nethelpers.h"
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

lru_cache_t *cache;

int register_with_cnf(char *cnf_ip, in_port_t shard_port);
int run(in_port_t shard_port);
void handle_connection(int client_socket, IA client_addr);
void handle_put(uint8_t *payload);
void handle_get(int socket, uint8_t *payload);

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

int register_with_cnf(char *cnf_addr, in_port_t shard_port) {

  int cnf_socket = connect_to_socket(cnf_addr, CNFPORT);

  uint8_t payload[2];
  pack_short(shard_port, payload);
  send_msg(cnf_socket, (CanaryMsg){.type = RegisterShard2Cnf,
                                   .payload_len = 2,
                                   .payload = payload});
  CanaryMsg msg;
  receive_msg(cnf_socket, &msg);

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
  SA_IN client_addr;

  shard_socket = bind_n_listen_socket(shard_port, BACKLOG);

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
  case GetClient2Shard:
    handle_get(socket, msg.payload);
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
}

void handle_put(uint8_t *payload) {
  char *key;
  int value;

  unpack_string_int(&key, &value, payload);
  lru_entry_t *removed = put(cache, key, value);

  printf("Put key value pair (%s, %d) ", key, value);
  if (removed != NULL) {
    printf("and expelled key value pair (%s, %d) from cache\n", removed->key,
           removed->value);
  } else {
    printf("in cache\n");
  }
}

void handle_get(int socket, uint8_t *payload) {
  CanaryMsg msg = {.type = GetShard2Client};

  char *key = (char *)payload;

  printf("Client request value for key %s\n", key);
  int *value = get(cache, key);

  if (value == NULL) {
    printf("No value found\n\n");
    msg.payload_len = 0;
  } else {
    printf("Found value %d\n\n", *value);
    msg.payload_len = sizeof(int);
    msg.payload = (uint8_t *)value;
  }
  send_msg(socket, msg);
}
