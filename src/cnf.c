#include "../lib/cproto/cproto.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CNFPORT 8080
#define BACKLOG 100

#define MAXSHARDS 2

CanaryShardInfo shards[MAXSHARDS];
int num_shards = 0;

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;
typedef struct in_addr IA;

void handle_args(int argc, char *argv);
void run();

void handle_connection(int client_socket, IA client_addr);
void handle_shard_registration(int socket, uint8_t *payload, IA shard_addr);

int main(int argc, char *argv[]) {
  srandom(time(NULL));
  run();
  return 0;
}

void run() {
  int server_socket, client_socket, addr_size;
  SA_IN server_addr, client_addr;

  if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Error creating socket: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(CNFPORT);

  if ((bind(server_socket, (SA *)&server_addr, sizeof(server_addr))) < 0) {
    printf("Error binding socket: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((listen(server_socket, BACKLOG)) == -1) {
    printf("Error during listen\n");
    exit(EXIT_FAILURE);
  }

  while (1) {
    printf("Waiting for connections...\n");
    addr_size = sizeof(SA_IN);

    if ((client_socket = accept(server_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      printf("accept failed!\n");
      continue;
    }

    printf("IP of client is: %s\n", inet_ntoa(client_addr.sin_addr));
    printf("Port of client is: %d\n", ntohs(client_addr.sin_port));
    handle_connection(client_socket, client_addr.sin_addr);
  }
}

void handle_connection(int socket, IA client_addr) {
  CanaryMsg msg;

  if (receive_msg(socket, &msg) == -1) {
    send_error_msg(socket, "Could not receive message");
  }

  switch (msg.type) {
  case RegisterShard2Cnf:
    handle_shard_registration(socket, msg.payload, client_addr);
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
}

void handle_shard_registration(int socket, uint8_t *payload, IA addr) {
  in_port_t port;
  unpack_register_payload(&port, payload);
  if (num_shards >= MAXSHARDS) {
    printf("Could not register shard at %s:%d\n", inet_ntoa(addr), port);
    send_error_msg(socket, "Reached max shard capacity");
    return;
  }

  CanaryShardInfo shard = {.id = random(), .ip = addr, .port = port};
  shards[num_shards] = shard;
  num_shards++;
  qsort(shards, num_shards, sizeof(CanaryShardInfo), compare_shards);
  printf("Registered shard : {\n\tid: %ld,\n\tip: %s\n\tport: %d\n}\n",
         shard.id, inet_ntoa(shard.ip), shard.port);
  send_msg(socket, (CanaryMsg){.type = RegisterCnf2Mstr, .payload_len = 0});
}
