#include "../lib/connq/connq.h"
#include "../lib/cproto/cproto.h"
#include "../lib/lru//lru.h"
#include "../lib/nethelpers/nethelpers.h"
#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_CNF_PORT 8080
#define DEFAULT_CNF_ADDR "127.0.0.1"
#define DEFAULT_SHARD_PORT 6969
#define BACKLOG 100
#define MAX_THREADS 10
#define MAX_CACHE_CAPACITY 1000
#define HEARTBEAT_INTERVAL 10

typedef enum {
  Unassigned,
  Master,
  Follower,
} ShardRole;

ShardRole role = Unassigned;

char cnf_addr[20] = DEFAULT_CNF_ADDR;
in_port_t cnf_port = DEFAULT_CNF_PORT;

pthread_t thread_pool[MAX_THREADS], heartbeat;
conn_queue_t conn_q;
pthread_cond_t conn_q_cond;
pthread_mutex_t conn_q_lock;

lru_cache_t *cache;
pthread_mutex_t cache_lock;

int register_with_cnf(char *cnf_addr, in_port_t cnf_port, in_port_t shard_port);
int run(in_port_t shard_port);

void *worker_thread(void *arg);
void *heartbeat_thread(void *arg);

void handle_connection(conn_ctx_t *ctx);
void handle_put(uint8_t *payload);
void handle_get(int socket, uint8_t *payload);

int main(int argc, char *argv[]) {
  int opt;

  int num_threads = MAX_THREADS, cache_capacity = MAX_CACHE_CAPACITY;
  in_port_t shard_port = DEFAULT_SHARD_PORT;

  while ((opt = getopt(argc, argv, "p:P:a:c:t:")) != -1) {
    switch (opt) {
    case 'p':
      shard_port = atoi(optarg);
      break;
    case 'P':
      cnf_port = atoi(optarg);
      break;
    case 'a':
      memset(cnf_addr, 0, strlen(cnf_addr));
      strcpy(cnf_addr, optarg);
      break;
    case 'c':
      cache_capacity = atoi(optarg);
      cache_capacity = cache_capacity > MAX_CACHE_CAPACITY ? MAX_CACHE_CAPACITY
                                                           : cache_capacity;
      break;
    case 't':
      num_threads = atoi(optarg);
      num_threads = num_threads > MAX_THREADS ? MAX_THREADS : num_threads;
      break;
    default:
      printf("Usage: %s [-p <shard-port] [-P <cnf-port>] [-a <cnf-addr>] [-c "
             "<cache-capacity>] [-t <num-threads]\n",
             argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  cache = create_lru_cache(cache_capacity);
  conn_q = create_queue();

  if (register_with_cnf(cnf_addr, cnf_port, shard_port) == -1) {
    exit(EXIT_FAILURE);
  }

  for (long i = 0; i < num_threads; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, (void *)i);
  }

  if (run(shard_port) == -1)
    exit(EXIT_FAILURE);
}

int register_with_cnf(char *cnf_addr, in_port_t cnf_port,
                      in_port_t shard_port) {

  int cnf_socket = connect_to_socket(cnf_addr, cnf_port);

  uint8_t payload[2];
  pack_short(shard_port, payload);
  send_msg(cnf_socket, (CanaryMsg){.type = Mstr2CnfRegister,
                                   .payload_len = 2,
                                   .payload = payload});
  CanaryMsg msg;
  receive_msg(cnf_socket, &msg);

  switch (msg.type) {
  case Cnf2MstrRegister:
    role = Master;
    printf("Successfully registered shard as a master shard.\n");
    break;
  case Error:
    printf("Failed to register shard: %s\n", msg.payload);
    return -1;
  default:
    printf("Received wrong message type %d\n", msg.type);
    return -1;
  }
  uint32_t *id = malloc(sizeof(uint32_t));
  memcpy(id, msg.payload, sizeof(uint32_t));
  pthread_create(&heartbeat, NULL, heartbeat_thread, (void *)id);
  return 0;
}

void *worker_thread(void *arg) {
  long tid = (long)arg;
  while (1) {
    conn_ctx_t *ctx;

    // CRITICAL SECTION BEGIN
    pthread_mutex_lock(&conn_q_lock);
    if ((ctx = dequeue(&conn_q)) == NULL) {
      pthread_cond_wait(&conn_q_cond, &conn_q_lock);

      // retry
      ctx = dequeue(&conn_q);
    }
    pthread_mutex_unlock(&conn_q_lock);
    // CRITICAL SECTION END

    printf("Thread: %ld is handling connection from %s:%d\n", tid,
           inet_ntoa(ctx->client_addr), ctx->port);

    handle_connection(ctx);
  }
}

void *heartbeat_thread(void *arg) {
  int socket;

  uint32_t id = *(uint32_t *)arg;
  int payload_len = sizeof(id);
  uint8_t *payload = (uint8_t *)&id;
  CanaryMsg msg = {.type = Shard2CnfHeartbeat,
                   .payload_len = payload_len,
                   .payload = payload};

  free(arg);
  while (1) {
    sleep(HEARTBEAT_INTERVAL);
    if ((socket = connect_to_socket(cnf_addr, cnf_port)) == -1) {
      printf("Heartbeat thread could not connect to cnf\n");
      continue;
    }
    send_msg(socket, msg);
    close(socket);
  }
}

int run(in_port_t shard_port) {
  int shard_socket, client_socket, addr_size;
  SA_IN client_addr;

  if ((shard_socket = bind_n_listen_socket(shard_port, BACKLOG)) == -1)
    return -1;

  while (1) {
    printf("Waiting for connections...\n\n");
    addr_size = sizeof(SA_IN);

    if ((client_socket = accept(shard_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      printf("Accept failed\n\n");
      continue;
    }

    conn_ctx_t *ctx = malloc(sizeof(conn_ctx_t));
    *ctx = (conn_ctx_t){.socket = client_socket,
                        .client_addr = client_addr.sin_addr,
                        .port = client_addr.sin_port};

    // CRITICAL SECTION BEGIN
    pthread_mutex_lock(&conn_q_lock);
    enqueue(&conn_q, ctx);
    pthread_cond_signal(&conn_q_cond);
    pthread_mutex_unlock(&conn_q_lock);
    // CRITICAL SECTION END
  }
}

void handle_connection(conn_ctx_t *ctx) {
  int socket = ctx->socket;
  CanaryMsg msg;

  free(ctx);

  if (receive_msg(socket, &msg) == -1) {
    send_error_msg(socket, "Could not receive message");
    return;
  }

  switch (msg.type) {
  case Client2MstrPut:
    handle_put(msg.payload);
    break;
  case Client2ShardGet:
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

  // BEGIN CRITICAL SECTION
  pthread_mutex_lock(&cache_lock);
  lru_entry_t *removed = put(cache, key, value);
  pthread_mutex_unlock(&cache_lock);
  // END CRITICAL SECTION

  printf("Put key value pair (%s, %d) ", key, value);
  if (removed != NULL) {
    printf("and expelled key value pair (%s, %d) from cache\n", removed->key,
           removed->value);
    free(removed);
  } else {
    printf("in cache\n");
  }
}

void handle_get(int socket, uint8_t *payload) {
  CanaryMsg msg = {.type = Shard2ClientGet};

  char *key = (char *)payload;

  printf("Client request value for key %s\n", key);

  // BEGIN CRITICAL SECTION
  pthread_mutex_lock(&cache_lock);
  int *value = get(cache, key);
  pthread_mutex_unlock(&cache_lock);
  // END CRITICAL SECTION

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
