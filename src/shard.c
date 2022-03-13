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

// ---------------- DEFAULT VALUES ----------------
#define DEFAULT_CNF_PORT 8080
#define DEFAULT_CNF_ADDR "127.0.0.1"
#define DEFAULT_SHARD_PORT 6969
#define BACKLOG 100
#define MAX_THREADS 10
#define MAX_CACHE_CAPACITY 1000
#define HEARTBEAT_INTERVAL 10

// ---------------- FUNCTION PROTOTYPES ------------

// Runner functions.
int register_with_cnf(char *cnf_addr, in_port_t cnf_port, in_port_t shard_port);
int run(in_port_t shard_port);

// Thread functions.
void *worker_thread(void *arg);
void *master_heartbeat_thread(void *arg);

// Handlers.
void handle_connection(conn_ctx_t *ctx);
void handle_put(uint8_t *payload);
void handle_get(int socket, uint8_t *payload);

// ---------------- CUSTOM TYPES ------------------

typedef enum {
  Master,
  Follower,
} ShardRole;

// ---------------- GLOBAL VARIABLES --------------

ShardRole role = Master;

// Address and port of configuration service.
char cnf_addr[20] = DEFAULT_CNF_ADDR;
in_port_t cnf_port = DEFAULT_CNF_PORT;

// Variables related to threading.

// Thread pool variables.
conn_queue_t conn_q;
pthread_t thread_pool[MAX_THREADS], heartbeat;
pthread_cond_t conn_q_cond;
pthread_mutex_t conn_q_lock;

// local LRU cache protected by mutex.
lru_cache_t *cache;
pthread_mutex_t cache_lock;

// ---------------- IMPLEMENTATION -----------------

/**
 * @brief Runner function.
 *
 * - Parses command line arguments into local/global values.
 * - Registers shard with configuration service
 * - Runs the shard socket server.
 *
 */
int main(int argc, char *argv[]) {
  int opt;
  int num_threads = MAX_THREADS, cache_capacity = MAX_CACHE_CAPACITY;
  in_port_t shard_port = DEFAULT_SHARD_PORT;

  // Parse flags
  while ((opt = getopt(argc, argv, "p:P:a:c:t:f")) != -1) {
    switch (opt) {
    case 'p':
      shard_port = atoi(optarg);
      break;
    case 'P':
      cnf_port = atoi(optarg);
      break;
    case 'a':
      memset(cnf_addr, 0, strlen(cnf_addr) + 1);
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
    case 'f':
      role = Follower;
      break;
    default:
      printf("Usage: %s [-p <shard-port] [-P <cnf-port>] [-a <cnf-addr>] [-c "
             "<cache-capacity>] [-t <num-threads], [-f]\n",
             argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  // Initialize local LRU cache.
  cache = create_lru_cache(cache_capacity);

  // Register shard with configuration service.
  if (register_with_cnf(cnf_addr, cnf_port, shard_port) == -1) {
    printf("Could not register with configuration service\n");
    exit(EXIT_FAILURE);
  }

  // Create connection queue and worker thread.
  conn_q = create_queue();
  for (long i = 0; i < num_threads; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, (void *)i);
  }

  // Run socket server.
  if (run(shard_port) == -1) {
    printf("Could not run socket server\n");
    exit(EXIT_FAILURE);
  }
}

// RUNNER FUNCTIONS

/**
 * @brief Registers shard with configuration service and creates heartbeat
 * thread.
 *
 * @param cnf_addr - char*
 * @param cnf_port - in_port_t
 * @param shard_port -in_port_t
 * @return -1 in case of error, 0 otherwise.
 */
int register_with_cnf(char *cnf_addr, in_port_t cnf_port,
                      in_port_t shard_port) {
  CanaryMsg req, resp;
  int cnf_socket = connect_to_socket(cnf_addr, cnf_port);

  uint8_t payload[2];
  pack_short(shard_port, payload);

  req = (CanaryMsg){.payload_len = 2, .payload = payload};

  if (role == Master) {
    req.type = Mstr2CnfRegister;
  } else {
    req.type = Flwr2CnfRegister;
  }
  send_msg(cnf_socket, req);
  receive_msg(cnf_socket, &resp);

  switch (resp.type) {
  case Cnf2MstrRegister: {
    uint32_t *id = malloc(sizeof(uint32_t));
    memcpy(id, resp.payload, sizeof(uint32_t));
    pthread_create(&heartbeat, NULL, master_heartbeat_thread, (void *)id);
    printf("Successfully registered shard as a master shard\n");
    return 0;
  }
  case Cnf2FlwrRegister:
    printf("Successfully registered shard as follower shard\n");
    return 0;
  case Error:
    printf("Failed to register shard: %s\n", resp.payload);
    return -1;
  default:
    printf("Received wrong message type %d\n", resp.type);
    return -1;
  }
  close(cnf_socket);
  return 0;
}

/**
 * @brief Runs the multithreaded socket server. Will accept a socket connection
 * and put it on the connection queue for a worker thread to pick up.
 *
 * @param shard_port - in_port_t
 * @return -1 in case of error, 0 otherwise.
 */
int run(in_port_t shard_port) {
  int shard_socket, client_socket, addr_size;
  SA_IN client_addr;

  if ((shard_socket = bind_n_listen_socket(shard_port, BACKLOG)) == -1)
    return -1;

  while (1) {
    addr_size = sizeof(SA_IN);
    if ((client_socket = accept(shard_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      printf("Accept failed\n");
      continue;
    }

    // Allocate on heap, freed by worker.
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

// THREAD FUNCTIONS

/**
 * @brief Will grab a connection of the queue and handle it accordingly.
 *
 * @param arg - void *
 */
void *worker_thread(void *arg) {
  long tid = (long)arg; // id for logging.
  while (1) {
    conn_ctx_t *ctx;

    // CRITICAL SECTION BEGIN
    pthread_mutex_lock(&conn_q_lock);
    if ((ctx = dequeue(&conn_q)) == NULL) {
      // Suspend if there is no work.
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

/**
 * @brief Will periodically send a heartbeat to the configuration service.
 *
 * @param arg - void *
 * @return
 */
void *master_heartbeat_thread(void *arg) {
  int socket;

  // As the message payload is always identical we can create it in advance.
  uint32_t id = *(uint32_t *)arg;
  int payload_len = sizeof(id);
  uint8_t *payload = (uint8_t *)&id;
  CanaryMsg msg = {.type = Shard2CnfHeartbeat,
                   .payload_len = payload_len,
                   .payload = payload};

  free(arg);
  // sleep -> send message -> sleep ...
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

// HANDLERS

/**
 * @brief Will handle a socket connection. Will in turn multiplex out to other
 * handlers depending on what type of message is received.
 *
 * @param ctx - conn_ctx_t
 */
void handle_connection(conn_ctx_t *ctx) {
  int socket = ctx->socket;
  CanaryMsg msg;

  free(ctx); // we have copied the necessary data.

  if (receive_msg(socket, &msg) == -1) {
    send_error_msg(socket, "Could not receive message");
    return;
  }

  // Multiplex out to other handlers.
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

/**
 * @brief Handles a `put` operation by a client.
 *
 * @param payload - uint8_t *
 */
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

/**
 * @brief Handles a `get` operation by a client.
 *
 * @param socket - int
 * @param payload - uint8_t *
 */
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
