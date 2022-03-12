#include "../lib/connq/connq.h"
#include "../lib/cproto/cproto.h"
#include "../lib/hashing//hashing.h"
#include "../lib/nethelpers/nethelpers.h"
#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CNF_PORT 8080
#define BACKLOG 100

#define MAXSHARDS 100
#define MAXTHREADS 10

CanaryShardInfo shards[MAXSHARDS];

pthread_t thread_pool[MAXTHREADS];

conn_queue_t conn_q;
pthread_cond_t conn_q_cond;
pthread_mutex_t conn_q_lock;

int num_shards = 0;

int run(in_port_t port);
void *worker_thread(void *arg);
void handle_connection(conn_ctx_t *ctx);
void handle_shard_registration(int socket, uint8_t *payload, IA shard_addr);
void handle_shard_selection(int socket, uint8_t *payload);

int main(int argc, char *argv[]) {
  int opt;
  in_port_t port = DEFAULT_CNF_PORT;
  int num_threads = MAXTHREADS;

  srand(time(NULL));

  while ((opt = getopt(argc, argv, "p:t") != -1)) {
    switch (opt) {
    case 'p':
      port = atoi(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      num_threads = num_threads > MAXTHREADS ? MAXTHREADS : num_threads;
      break;
    default:
      printf("Usage: %s [-p <cnf-port>] [-t <num-threads>]\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  for (long i = 0; i < num_threads; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, (void *)i);
  }

  if (run(port) == -1)
    exit(EXIT_FAILURE);

  return 0;
}

int run(in_port_t port) {
  int server_socket, client_socket, addr_size;
  SA_IN client_addr;

  server_socket = bind_n_listen_socket(port, BACKLOG);

  while (1) {
    printf("Waiting for connections...\n\n");
    addr_size = sizeof(SA_IN);

    if ((client_socket = accept(server_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      printf("accept failed!\n");
      continue;
    }

    conn_ctx_t *ctx = malloc(sizeof(conn_ctx_t));
    *ctx = (conn_ctx_t){.socket = client_socket,
                        .client_addr = client_addr.sin_addr,
                        .port = client_addr.sin_port};

    pthread_mutex_lock(&conn_q_lock);
    enqueue(&conn_q, ctx);
    pthread_cond_signal(&conn_q_cond);
    pthread_mutex_unlock(&conn_q_lock);
  }
}

void *worker_thread(void *arg) {
  long tid = (long)arg;
  while (1) {
    conn_ctx_t *ctx;
    pthread_mutex_lock(&conn_q_lock);

    if ((ctx = dequeue(&conn_q)) == NULL) {
      pthread_cond_wait(&conn_q_cond, &conn_q_lock);

      // retry
      ctx = dequeue(&conn_q);
    }
    pthread_mutex_unlock(&conn_q_lock);
    printf("Thread: %ld is handling connection from %s:%d\n", tid,
           inet_ntoa(ctx->client_addr), ctx->port);
    handle_connection(ctx);
  }
}

void handle_connection(conn_ctx_t *ctx) {
  int socket = ctx->socket;
  IA client_addr = ctx->client_addr;
  CanaryMsg msg;

  free(ctx);

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
