#include "../lib/cproto/cproto.h"
#include "../lib/hashing//hashing.h"
#include "../lib/nethelpers/nethelpers.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CNFPORT 8080
#define BACKLOG 100

#define MAXSHARDS 100

CanaryShardInfo shards[MAXSHARDS];
int num_shards = 0;

int run();

void handle_connection(int client_socket, IA client_addr);
void handle_shard_registration(int socket, uint8_t *payload, IA shard_addr);
void handle_shard_selection(int socket, uint8_t *payload);

int main(int argc, char *argv[]) {
  srand(time(NULL));
  if (run() == -1)
    exit(EXIT_FAILURE);

  return 0;
}

int run() {
  int server_socket, client_socket, addr_size;
  SA_IN client_addr;

  server_socket = bind_n_listen_socket(CNFPORT, BACKLOG);

  while (1) {
    printf("Waiting for connections...\n\n");
    addr_size = sizeof(SA_IN);

    if ((client_socket = accept(server_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      printf("accept failed!\n");
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
  case RegisterShard2Cnf:
    handle_shard_registration(socket, msg.payload, client_addr);
    break;
  case ShardInfoClient2Cnf:
    handle_shard_selection(socket, msg.payload);
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
}

void handle_shard_registration(int socket, uint8_t *payload, IA addr) {
  in_port_t port;
  unpack_short(&port, payload);
  if (num_shards >= MAXSHARDS) {
    printf("Could not register shard at %s:%d\n", inet_ntoa(addr), port);
    send_error_msg(socket, "Reached max shard capacity");
    return;
  }
  CanaryShardInfo shard = {.id = rand64(), .ip = addr, .port = port};
  shards[num_shards] = shard;
  num_shards++;
  qsort(shards, num_shards, sizeof(CanaryShardInfo), compare_shards);
  printf("Registered shard : {\n\tid: %lu,\n\tip: %s\n\tport: %d\n}\n",
         shard.id, inet_ntoa(shard.ip), shard.port);
  send_msg(socket, (CanaryMsg){.type = RegisterCnf2Mstr, .payload_len = 0});
}

void handle_shard_selection(int socket, uint8_t *payload) {
  // cast payload to string and hash it.
  char *key = (char *)payload;
  size_t hash = hash_djb2(key);

  // Binary search to find the first shard.id > hash.
  int start = 0, end = num_shards, middle;
  while (start <= end) {
    middle = start + (end - start) / 2;

    if (shards[middle].id <= hash) {
      start = middle + 1;
    } else {
      end = middle - 1;
    }
  }

  // If we cant find an element s.t shard.id > hash, we wrap around to zero.
  int idx = start > num_shards ? 0 : start;

  // Pack the payload and send the message to client.
  char *ip_str = inet_ntoa(shards[idx].ip);
  in_port_t port = shards[idx].port;
  uint32_t ip_len = strlen(ip_str);
  uint32_t buf_len = sizeof(ip_len) + ip_len + sizeof(port);
  uint8_t *buf = malloc(buf_len);

  pack_string_short(ip_str, ip_len, port, buf);

  CanaryMsg msg = {
      .type = ShardInfoCnf2Client, .payload_len = buf_len, .payload = buf};

  printf("hash = %lu\n", hash);
  printf("Shard ids:\n");
  for (int i = 0; i < num_shards; i++) {
    printf("{id=%lu, port=%d}", shards[i].id, shards[i].port);
    if (i < num_shards - 1)
      printf(" -> ");
  }
  printf("\n");

  send_msg(socket, msg);
  printf(
      "Notified that shard at %s:%d has responsibility of key %s to client\n",
      ip_str, port, key);
}
