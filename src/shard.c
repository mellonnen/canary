#include "../lib/connq/connq.h"
#include "../lib/cproto/cproto.h"
#include "../lib/logger/logger.h"
#include "../lib/lru//lru.h"
#include "../lib/nethelpers/nethelpers.h"
#include "constants.h"
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
#define DEFAULT_SHARD_PORT 6969
#define MAX_CACHE_CAPACITY 1000
// ---------------- CUSTOM TYPES ------------------

typedef enum {
  Master,
  Follower,
} ShardRole;

typedef struct {
  IA addr;
  in_port_t port;
} follower_t;

typedef struct {
  uint8_t *payload;
  uint32_t payload_len;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  bool consume;
} follower_chan_t;

// ---------------- FUNCTION PROTOTYPES ------------

// Runner functions.
int register_with_cnf(char *cnf_addr, in_port_t cnf_port, in_port_t shard_port);
int run(in_port_t shard_port);

// Thread functions.
void *worker_thread(void *arg);
void *master_heartbeat_thread(void *arg);
void *follower_heartbeat_thread(void *arg);

// Handlers.
void handle_connection(conn_ctx_t *ctx);
void handle_put(uint8_t *payload, uint32_t payload_len);
void handle_get(int socket, uint8_t *payload);
void handle_flwr_connection(int socket, IA addr, uint8_t *payload);
void handle_replication(int socket, uint8_t *payload, uint32_t payload_len);
void handle_promotion();
void handle_redirection(uint8_t *payload);

// ---------------- GLOBAL VARIABLES --------------

ShardRole role = Master;
pthread_rwlock_t role_lock;

// Address and port of configuration service.
char cnf_addr[20] = DEFAULT_CNF_ADDR;
in_port_t cnf_port = DEFAULT_CNF_PORT;

int num_flwrs = 0;
follower_t *flwrs[MAX_FLWR_PER_MASTER] = {NULL};
pthread_mutex_t flwr_lock;

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
    logfmt("Could not register with configuration service");
    exit(EXIT_FAILURE);
  }

  // Create connection queue and worker thread.
  conn_q = create_queue();
  for (long i = 0; i < num_threads; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, (void *)i);
  }

  logfmt("starting data shard server at port %d", shard_port);
  // Run socket server.
  if (run(shard_port) == -1) {
    logfmt("could not run shard server");
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

  uint8_t payload[sizeof(in_port_t)];
  pack_short(shard_port, payload);

  req = (CanaryMsg){.payload_len = 2, .payload = payload};

  // no need to use lock here as threads have not been started.
  if (role == Master) {
    req.type = Mstr2CnfRegister;
  } else {
    req.type = Flwr2CnfRegister;
  }
  send_msg(cnf_socket, req);
  receive_msg(cnf_socket, &resp);
  close(cnf_socket);

  switch (resp.type) {
  case Cnf2MstrRegister:
    pthread_create(&heartbeat, NULL, master_heartbeat_thread,
                   (void *)resp.payload);
    logfmt("Successfully registered shard as a master shard");
    free(resp.payload);
    return 0;
  case Cnf2FlwrRegister: {
    char *mstr_addr;
    in_port_t mstr_port;
    unpack_string_short(&mstr_addr, &mstr_port,
                        (resp.payload + sizeof(uint32_t) * 2));
    pthread_create(&heartbeat, NULL, follower_heartbeat_thread,
                   (void *)resp.payload);
    int mstr_socket = connect_to_socket(mstr_addr, mstr_port);
    send_msg(mstr_socket, (CanaryMsg){.type = Flwr2MstrConnect,
                                      .payload_len = sizeof(in_port_t),
                                      .payload = payload});

    free(resp.payload);
    logfmt("Successfully registered shard as a follower shard");
    return 0;
  }
  case Error:
    logfmt("Failed to register shardd due to: %s", resp.payload);
    break;
  default:
    logfmt("Received wrong message type %d", resp.type);
    break;
  }

  free(resp.payload);
  return -1;
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
      logfmt("Accept failed");
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
  CanaryMsg msg = {.type = Mstr2CnfHeartbeat,
                   .payload_len = payload_len,
                   .payload = payload};

  // sleep -> send message -> sleep ...
  while (1) {
    sleep(HEARTBEAT_INTERVAL);
    if ((socket = connect_to_socket(cnf_addr, cnf_port)) == -1) {
      logfmt("Heartbeat thread could not connect to configuration service");
      continue;
    }
    send_msg(socket, msg);
    close(socket);
  }
}

/**
 * @brief Will periodically send a heartbeat to the configuration service.
 *
 * @param arg
 * @return
 */
void *follower_heartbeat_thread(void *arg) {
  int socket, payload_len = sizeof(uint32_t) * 2;

  // copy the data to stack as pointer will be freed by other function call.
  uint8_t *cnf_payload = (uint8_t *)arg;
  uint8_t payload[payload_len];
  memcpy(payload, cnf_payload, payload_len);

  CanaryMsg msg = {.type = Flwr2CnfHeartbeat,
                   .payload_len = payload_len,
                   .payload = payload};

  // sleep -> send message -> sleep ...
  while (1) {
    sleep(HEARTBEAT_INTERVAL);
    // BEGIN CRITICAL SECTION
    pthread_rwlock_rdlock(&role_lock);
    if (role == Master) {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      uint8_t *mstr_payload = malloc(sizeof(uint32_t));
      memcpy(mstr_payload, payload, sizeof(uint32_t));
      close(socket);
      master_heartbeat_thread((void *)mstr_payload);
    }
    // END CRITICAL SECTION
    pthread_rwlock_unlock(&role_lock);
    if ((socket = connect_to_socket(cnf_addr, cnf_port)) == -1) {
      logfmt("Heartbeat thread could not connect to configuration service");
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
  IA client_addr = ctx->client_addr;
  CanaryMsg msg;

  free(ctx); // we have copied the necessary data.

  if (receive_msg(socket, &msg) == -1) {
    send_error_msg(socket, "Could not receive message");
    return;
  }

  // Multiplex out to other handlers.
  switch (msg.type) {
  case Client2MstrPut:

    // BEGIN CRITICAL SECTION
    pthread_rwlock_rdlock(&role_lock);
    if (role != Master) {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION

      logfmt("follower received put message");
    } else {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION

      handle_put(msg.payload, msg.payload_len);
    }
    break;
  case Mstr2FlwrReplicate:
    // BEGIN CRITICAL SECTION
    pthread_rwlock_rdlock(&role_lock);
    if (role != Follower) {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION

      logfmt("master received replication message");
    } else {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION

      handle_put(msg.payload, msg.payload_len);
    }
    break;
  case Client2ShardGet:
    handle_get(socket, msg.payload);
    break;
  case Flwr2MstrConnect:
    // BEGIN CRITICAL SECTION
    pthread_rwlock_rdlock(&role_lock);
    if (role != Master) {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      logfmt("follower shard received follower connection");
    } else {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      handle_flwr_connection(socket, client_addr, msg.payload);
    }
    break;
  case Cnf2FlwrPromote:
    // BEGIN CRITICAL SECTION
    pthread_rwlock_rdlock(&role_lock);
    if (role != Follower) {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      logfmt("master shard received promotion connection");
    } else {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      handle_promotion();
    }
    break;
  case Cnf2FlwrRedirect:
    // BEGIN CRITICAL SECTION
    pthread_rwlock_rdlock(&role_lock);
    if (role != Follower) {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      logfmt("master shard received promotion connection");
    } else {
      pthread_rwlock_unlock(&role_lock);
      // END CRITICAL SECTION
      handle_redirection(msg.payload);
    }
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
  close(socket);
}

/**
 * @brief Handles a `put` operation by a client. If the role of the shard is
 * `Master` it will send the payload to all listening follower channels.
 *
 * @param payload - uint8_t *
 */
void handle_put(uint8_t *payload, uint32_t payload_len) {
  char *key;
  int value;

  unpack_string_int(&key, &value, payload);

  // BEGIN CRITICAL SECTION
  pthread_mutex_lock(&cache_lock);
  lru_entry_t *removed = put(cache, key, value);
  pthread_mutex_unlock(&cache_lock);
  // END CRITICAL SECTION

  logfmt("Put key value pair (%s, %d)", key, value);
  if (removed != NULL) {
    logfmt("expelled key value pair (%s, %d) from cache", removed->key,
           removed->value);
    free(removed);
  }

  // If master replicate tho followers

  // BEGIN CRITICAL SECTION
  pthread_rwlock_rdlock(&role_lock); // in theory this lock is not needed as a
                                     // master cannot be "demoted"
  if (role == Master) {

    // BEGIN CRITICAL SECTION
    pthread_mutex_lock(&flwr_lock);
    for (int i = 0; i < MAX_FLWR_PER_MASTER; i++) {
      if (flwrs[i] == NULL)
        continue;

      int socket = connect_to_socket(inet_ntoa(flwrs[i]->addr), flwrs[i]->port);
      if (socket == -1) {
        free(flwrs[i]);
        flwrs[i] = NULL;
      }
      send_msg(socket, (CanaryMsg){.type = Mstr2FlwrReplicate,
                                   .payload_len = payload_len,
                                   .payload = payload});
      close(socket);
    }
    pthread_mutex_unlock(&flwr_lock);
    // END CRITICAL SECTION
  }
  pthread_rwlock_unlock(&role_lock);
  // END CRITICAL SECTION
  free(payload);
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

  // BEGIN CRITICAL SECTION
  pthread_mutex_lock(&cache_lock);
  int *value = get(cache, key);
  pthread_mutex_unlock(&cache_lock);
  // END CRITICAL SECTION

  if (value == NULL) {
    logfmt("no found for key %s in cache", key);
    msg.payload_len = 0;
  } else {
    logfmt("value %d found for key %s in cache", *value, key);
    msg.payload_len = sizeof(int);
    msg.payload = (uint8_t *)value;
  }
  send_msg(socket, msg);
  free(payload);
}

/**
 * @brief Will register a new follower
 *
 *
 * @param socket - int
 */
void handle_flwr_connection(int socket, IA addr, uint8_t *payload) {
  in_port_t port;
  unpack_short(&port, payload);
  free(payload);
  int idx = -1;
  follower_t *flwr;

  // BEGIN CRITICAL SECTION
  pthread_mutex_lock(&flwr_lock);

  if (num_flwrs >= MAX_FLWR_PER_MASTER) {
    send_error_msg(socket, "Follower capacity reached");
  } else {
    flwr = malloc(sizeof(follower_t));
    *flwr = (follower_t){.addr = addr, .port = port};

    for (idx = 0; idx < MAX_FLWR_PER_MASTER; idx++) {
      if (flwrs[idx] == NULL) {
        flwrs[idx] = flwr;
        num_flwrs++;
        break;
      }
    }
  }
  pthread_mutex_unlock(&flwr_lock);
  // END CRITICAL SECTION
}

/**
 * @brief replicates the cache from the master.
 *
 * @param socket - int
 * @param payload  - uint8_t *
 * @param payload_len  - uint32_t
 */
void handle_replication(int socket, uint8_t *payload, uint32_t payload_len) {
  logfmt("replicating data from master shard");
  handle_put(payload, payload_len);
}

void handle_promotion() {
  pthread_rwlock_wrlock(&role_lock);
  role = Master;
  pthread_rwlock_unlock(&role_lock);
  logfmt("shard has been promoted to master");
}

void handle_redirection(uint8_t *payload) {
  char *mstr_addr;
  in_port_t mstr_port;
  unpack_string_short(&mstr_addr, &mstr_port, payload);
  free(payload);

  int socket;
  if ((socket = connect_to_socket(mstr_addr, mstr_port)) == -1) {
    free(mstr_addr);
    return;
  }
  send_msg(socket, (CanaryMsg){.type = Flwr2MstrConnect, .payload_len = 0});
  CanaryMsg resp;
  receive_msg(socket, &resp);

  if (resp.type == Error) {
    logfmt("unable to redirect to new master shard due to: %s", resp.payload);
    free(resp.payload);
    free(mstr_addr);
    exit(EXIT_FAILURE);
  }
  logfmt("redirected to new master shard at %s:%d ", mstr_addr, mstr_port);
  free(resp.payload);
  free(mstr_addr);
}
