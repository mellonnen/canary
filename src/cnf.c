#include "cproto.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CNFPORT 8080
#define BACKLOG 100
typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void run_server();

int main(int argc, char *argv[]) {
  run_server();
  return 0;
}

void handle_connection(int client_socket) {
  CanaryMsg *msg = receive_msg(client_socket);
  printf("message type received: %d\n", msg->type);
  printf("payload length received: %d\n", msg->payload_len);

  printf("payload received: %s\n", (char *)msg->payload);
}

void run_server() {
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

    handle_connection(client_socket);
  }
}
