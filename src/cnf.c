#include "../lib/cproto/cproto.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CNFPORT 8080
#define BACKLOG 100

#define MAXSHARDS 100

CanaryShardInfo shards[MAXSHARDS];

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void handle_args(int argc, char *argv);
void run();

void handle_connection(int client_socket);
void handle_shard_registration(uint8_t *buf, uint32_t buf_size);

int main(int argc, char *argv[]) {
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
    handle_connection(client_socket);
  }
}

void handle_connection(int client_socket) {
  CanaryMsg msg;

  if (receive_msg(client_socket, &msg) == -1) {
    send_error_msg(client_socket, "Could not receive message");
  }

  switch (msg.type) {
  case RegisterShard2Cnf:
    handle_shard_registration(msg.payload, msg.payload_len);
    break;
  default:
    send_error_msg(client_socket, "Incorrect Canary message type");
    break;
  }
}

void handle_shard_registration(uint8_t *buf, uint32_t buf_len){};
