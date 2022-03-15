#include "../lib/connq/connq.h"
#include "../lib/cproto/cproto.h"
#include "../lib/hashing/hashing.h"
#include "../lib/logger/logger.h"
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

// ---------------- DEFAULT VALUES ----------------
#define DEFAULT_CNF_PORT 8080
#define BACKLOG 100
#define MAX_MASTER_SHARDS 100
#define MAX_FLWR_PER_MASTER 2
#define MAXTHREADS 10
#define HEARTBEAT_INTERVAL_WITH_SLACK 15
#define SHARD_MAITNENANCE_INTERVAL 30

// ---------------- CUSTOM TYPES ------------------

// Represents the "location" of a shard in the network.
typedef struct {
  IA addr;
  in_port_t port;
} shard_t;

// Represents follower shard.
typedef struct {
  shard_t shard;
  time_t expiration; // timestamp of when this shard expires.
} follower_shard_t;

// Represents Master shard.
typedef struct {
  uint32_t id; // randomly generated id for consistent hashing.
  shard_t shard;
  time_t expiration; // timestamp of when this shard expires.
  bool expired;      // flag that marks if this shard has expired.
  int num_flwrs;
  follower_shard_t *flwrs[MAX_FLWR_PER_MASTER];
} master_shard_t;

// ---------------- FUNCTION PROTOTYPES ------------

// Runner functions.
int run(in_port_t port);

// Thread functions.
void *worker_thread(void *arg);
void *shard_maintenance_thread(void *arg);

// Handlers.
void handle_connection(conn_ctx_t *ctx);
void handle_master_shard_registration(int socket, uint8_t *payload, IA addr);
void handle_flwr_shard_registration(int socket, uint8_t *payload, IA addr);
void handle_shard_discovery(int socket, uint8_t *payload);
void handle_master_shard_heartbeat(uint8_t *payload);
void handle_flwr_shard_heartbeat(uint8_t *payload);

// Utilities
int compare_shards(const void *a, const void *b);
master_shard_t *find_master_shard_by_id(uint32_t id);

// ---------------- GLOBAL VARIABLES --------------

// master shard array. ALWAYS in order of ids.
master_shard_t mstr_shards[MAX_MASTER_SHARDS];
pthread_rwlock_t shards_lock;

// Threading related variables.
pthread_t thread_pool[MAXTHREADS], shard_maintenance;
conn_queue_t conn_q;
pthread_cond_t conn_q_cond;
pthread_mutex_t conn_q_lock;

// Configurable values.
int num_mstr_shards = 0;
int max_mstr_shards = MAX_MASTER_SHARDS;
int flwr_per_master = 0;

// ---------------- IMPLEMENTATION -----------------

/**
 * @brief Runner code.
 *
 * - Parses commandline arguments int local/global values.
 * - Starts shard maintenance thread.
 * - Runs multithreaded socket server.
 *
 * @param argc - int
 * @param argv  char *[]
 * @return
 */
int main(int argc, char *argv[]) {
  srand(time(NULL));
  int opt;
  in_port_t port = DEFAULT_CNF_PORT;
  int num_threads = MAXTHREADS;

  // Parse flags.
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

  // Create threads.
  pthread_create(&shard_maintenance, NULL, shard_maintenance_thread, NULL);
  conn_q = create_queue();
  for (long i = 0; i < num_threads; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, NULL);
  }

  logfmt("Starting Configuration service on port %d", port);
  // Run server.
  if (run(port) == -1)
    exit(EXIT_FAILURE);

  return 0;
}

// RUNNER FUNCTIONS

/**
 * @brief Runs the multithreaded socket server. Will receive connections and add
 * them to a work queue.
 *
 * @param port - in_port_t
 * @return -1 if error occurred, 0 otherwise.
 */
int run(in_port_t port) {
  int server_socket, client_socket, addr_size;
  SA_IN client_addr;

  server_socket = bind_n_listen_socket(port, BACKLOG);

  while (1) {
    addr_size = sizeof(SA_IN);

    if ((client_socket = accept(server_socket, (SA *)&client_addr,
                                (socklen_t *)&addr_size)) == -1) {
      logfmt("accept failed");
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
 * @brief Will periodically go through shards and remove stale shards.
 *
 * TODO: must go through follower shards as well. Promote or remove.
 * @param arg - void *
 */
void *shard_maintenance_thread(void *arg) {
  while (1) {
    sleep(SHARD_MAITNENANCE_INTERVAL);
    // BEGIN CRITICAL SECTION
    pthread_rwlock_wrlock(&shards_lock);
    if (num_mstr_shards > 0) {
      // Scan through the shards and mark expired shards.
      int num_expired = 0;

      for (int i = 0; i < num_mstr_shards; i++) {
        for (int j = 0; j < MAX_FLWR_PER_MASTER; j++) {
          follower_shard_t *flwr = mstr_shards[i].flwrs[j];
          // Check follower shards expiration.
          if (flwr != NULL && flwr->expiration < time(NULL)) {

            logfmt("follower shard at %s:%d has expired",
                   inet_ntoa(mstr_shards[i].flwrs[j]->shard.addr),
                   mstr_shards[i].flwrs[j]->shard.port);
            // free pointer and sett slot to NULL
            free(mstr_shards[i].flwrs[j]);
            mstr_shards[i].flwrs[j] = NULL;
            // update local and global flwr count.
            mstr_shards[i].num_flwrs--;
            if (flwr_per_master > 0)
              flwr_per_master--;
          }
        }
        if (mstr_shards[i].expiration < time(NULL)) {
          // TODO: Promote shard here
          mstr_shards[i].expired = true;
          num_expired++;

          logfmt("master shard at %s:%d has expired",
                 inet_ntoa(mstr_shards[i].shard.addr),
                 mstr_shards[i].shard.port);
        }

        // Re-sort shards (expired will be put at the back).
        qsort(mstr_shards, num_mstr_shards, sizeof(master_shard_t),
              compare_shards);
        num_mstr_shards -= num_expired;
      }
    }
    pthread_rwlock_unlock(&shards_lock);
    // END CRITICAL SECTION
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

  free(ctx);

  if (receive_msg(socket, &msg) == -1) {
    send_error_msg(socket, "Could not receive message");
    return;
  }

  switch (msg.type) {
  case Mstr2CnfRegister:
    handle_master_shard_registration(socket, msg.payload, client_addr);
    break;
  case Flwr2CnfRegister:
    handle_flwr_shard_registration(socket, msg.payload, client_addr);
    break;
  case Client2CnfDiscover:
    handle_shard_discovery(socket, msg.payload);
    break;
  case Mstr2CnfHeartbeat:
    handle_master_shard_heartbeat(msg.payload);
    break;
  case Flwr2CnfHeartbeat:
    handle_flwr_shard_heartbeat(msg.payload);
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
}

/**
 * @brief Registers a new master shard
 *
 * @param socket - int
 * @param payload - uint8_t *
 * @param addr - IA
 */
void handle_master_shard_registration(int socket, uint8_t *payload, IA addr) {
  in_port_t port;
  unpack_short(&port, payload);

  free(payload);
  // create random id.
  uint32_t id = rand();
  master_shard_t mstr =
      (master_shard_t){.id = id,
                       .shard = {.addr = addr, .port = port},
                       .expiration = time(NULL) + HEARTBEAT_INTERVAL_WITH_SLACK,
                       .expired = false,
                       .num_flwrs = 0,
                       .flwrs = {NULL, NULL}};

  // CRITICAL SECTION BEGIN
  pthread_rwlock_rdlock(&shards_lock);
  if (num_mstr_shards >= max_mstr_shards) {
    logfmt("could not register shard at %s:%d", inet_ntoa(addr), port);
    send_error_msg(socket, "Reached max shard capacity");
    return;
  }

  // Add to the end of array and sort the array.
  mstr_shards[num_mstr_shards] = mstr;
  num_mstr_shards++;
  qsort(mstr_shards, num_mstr_shards, sizeof(master_shard_t), compare_shards);
  flwr_per_master = 0;
  pthread_rwlock_unlock(&shards_lock);
  // CRITICAL SECTION END

  logfmt("registered new master shard at %s:%d with id %d",
         inet_ntoa(mstr.shard.addr), mstr.shard.port, mstr.id);

  // respond to shard.
  uint32_t n_id = htonl(id);
  send_msg(socket, (CanaryMsg){.type = Cnf2MstrRegister,
                               .payload_len = sizeof(uint32_t),
                               .payload = (uint8_t *)&n_id});
}

/**
 * @brief Registers follower shard.
 *
 * @param socket - int
 * @param payload - uint8_t *
 * @param addr - IA
 */
void handle_flwr_shard_registration(int socket, uint8_t *payload, IA addr) {
  if (flwr_per_master >= MAX_FLWR_PER_MASTER) {
    send_error_msg(socket, "max capacity for follower shards reached");
    return;
  }

  in_port_t port;
  unpack_short(&port, payload);

  follower_shard_t *flwr = malloc(sizeof(follower_shard_t));

  flwr->shard = (shard_t){.addr = addr, .port = port};

  int mstr_idx = -1, flwr_idx = -1;
  // START CRITICAL SECTION
  pthread_rwlock_wrlock(&shards_lock);
  // Loop over the master shards, to distribute follower shards evenly.

  // EXAMPLE:  If we want a max of 2 followers per shard, we make sure that all
  // active master shard has at least 1 follower before we start we assign a
  // second follower to master shards.
  for (int i = 0; i < num_mstr_shards; i++) {
    // Check if this shard already has enough followers.
    if (mstr_shards[i].num_flwrs > 0 &&
        mstr_shards[i].num_flwrs > flwr_per_master)
      continue;

    // We are done with this "round".
    if (i == num_mstr_shards - 1)
      flwr_per_master++;

    // Find the empty follower slot.
    for (int j = 0; j < MAX_FLWR_PER_MASTER; j++) {
      if (mstr_shards[i].flwrs[j] != NULL)
        continue;

      flwr->expiration = time(NULL) + HEARTBEAT_INTERVAL_WITH_SLACK;
      mstr_shards[i].flwrs[j] = flwr;
      mstr_idx = i;
      flwr_idx = j;
      mstr_shards[i].num_flwrs++;

      logfmt("register follower shard at %s:%d to master shard with id %d",
             inet_ntoa(flwr->shard.addr), flwr->shard.port, mstr_shards[i].id);
      break;
    }
    break;
  }
  pthread_rwlock_unlock(&shards_lock);
  // END CRITICAL SECTION

  if (mstr_idx == -1 || flwr_idx == -1) {
    send_error_msg(socket, "Could not register shard as follower");
    return;
  }

  char *mstr_addr = inet_ntoa(mstr_shards[mstr_idx].shard.addr);
  in_port_t mstr_port = mstr_shards[mstr_idx].shard.port;
  uint32_t mstr_id = mstr_shards[mstr_idx].id;

  int buf_len = sizeof(mstr_id) + sizeof(flwr_idx) + sizeof(uint32_t) +
                strlen(mstr_addr) + 1 + sizeof(mstr_port);
  uint8_t *buf = malloc(buf_len);

  // pack indexes.
  pack_int_int(mstr_id, flwr_idx, buf);
  // pack mstr addr and port.
  pack_string_short(mstr_addr, strlen(mstr_addr) + 1, mstr_port,
                    (buf + sizeof(mstr_idx) + sizeof(flwr_idx)));

  send_msg(socket, (CanaryMsg){.type = Cnf2FlwrRegister,
                               .payload_len = buf_len,
                               .payload = buf});
}

/**
 * @brief Will tell a client which shard to turn to.
 *
 * TODO: should point to followers if it is a get request.
 * @param socket - int
 * @param payload - uint8_t *
 */
void handle_shard_discovery(int socket, uint8_t *payload) {
  // cast payload to string and hash it.
  char *op = (char *)payload;
  char *key = (char *)(payload + 4);
  bool op_is_get = strcmp(op, "get") == 0;
  if (strcmp(op, "put") != 0 && !op_is_get) {
    send_error_msg(socket, "invalid operation");
    free(payload);
    logfmt("received invalid operation %s from client", op);
    return;
  }
  size_t hash = hash_djb2(key) % RAND_MAX;

  // Binary search to find the first shard.id > hash.
  int start = 0, end = num_mstr_shards, middle;
  while (start <= end) {
    middle = start + (end - start) / 2;

    if (mstr_shards[middle].id <= hash) {
      start = middle + 1;
    } else {
      end = middle - 1;
    }
  }

  // If we cant find an element s.t shard.id > hash, we wrap around to zero.
  int idx = start > num_mstr_shards ? 0 : start;
  IA addr;
  in_port_t port;

  int r = rand() % (mstr_shards[idx].num_flwrs + 1);
  logfmt("r = %d", r);
  if (op_is_get && r != mstr_shards[idx].num_flwrs) {
    addr = mstr_shards[idx].flwrs[r]->shard.addr;
    port = mstr_shards[idx].flwrs[r]->shard.port;
    logfmt("client will be routed to follower shard");
  } else {
    logfmt("client will be routed to master shard");
    addr = mstr_shards[idx].shard.addr;
    port = mstr_shards[idx].shard.port;
  }

  // Pack the payload and send the message to client.
  char *addr_str = inet_ntoa(addr);
  uint32_t addr_len = strlen(addr_str) + 1;
  uint32_t buf_len = sizeof(addr_len) + addr_len + sizeof(port);
  uint8_t *buf = malloc(buf_len);

  pack_string_short(addr_str, addr_len, port, buf);

  CanaryMsg msg = {
      .type = Cnf2ClientDiscover, .payload_len = buf_len, .payload = buf};

  send_msg(socket, msg);
  logfmt("notified client that shard at %s:%d has responsibility of key %s",
         addr_str, port, key);
  free(buf);
}

/**
 * @brief Takes in a heartbeat and will update the expiration of the shard.
 *
 * @param payload - uint8_t;
 */
void handle_master_shard_heartbeat(uint8_t *payload) {
  uint32_t id = ntohl(*(uint32_t *)payload);
  free(payload);

  // BEGIN CRITICAL SECTION
  pthread_rwlock_wrlock(&shards_lock);
  master_shard_t *mstr = find_master_shard_by_id(id);

  if (mstr != NULL)
    mstr->expiration = time(NULL) + HEARTBEAT_INTERVAL_WITH_SLACK;
  pthread_rwlock_unlock(&shards_lock);
  // END CRITICAL SECTION
}

/**
 * @brief Takes in a heartbeat and will update the expiration of the shard.
 *
 * @param payload - uint8_t;
 */
void handle_flwr_shard_heartbeat(uint8_t *payload) {
  uint32_t mstr_id, flwr_idx;
  unpack_int_int(&mstr_id, &flwr_idx, payload);
  free(payload);

  // BEGIN CRITICAL SECTION
  pthread_rwlock_wrlock(&shards_lock);
  master_shard_t *mstr = find_master_shard_by_id(mstr_id);

  if (mstr != NULL)
    mstr->flwrs[flwr_idx]->expiration =
        time(NULL) + HEARTBEAT_INTERVAL_WITH_SLACK;
  pthread_rwlock_unlock(&shards_lock);
  // END CRITICAL SECTION
}

/**
 * @brief Comparator for `master_shard_t` meant to be used in `qsort`.
 *
 * @param a - void *
 * @param b - void *
 * @return 1 if a > b, -1 if a < b, 0 otherwise.
 */
int compare_shards(const void *a, const void *b) {
  master_shard_t *x = (master_shard_t *)a;
  master_shard_t *y = (master_shard_t *)b;
  if (x->expired)
    return 1;
  if (y->expired)
    return -1;

  if (x->id > y->id)
    return +1;
  if (x->id < y->id)
    return -1;
  return 0;
}

/**
 * @brief Finds the master shard by id using binary search.
 *
 * NOTE: Is not thread safe, should be executed in critical section.
 *
 * @param id - uint32_t
 * @return pointer to the master shard, NULL if shard could not bee found.
 */
master_shard_t *find_master_shard_by_id(uint32_t id) {
  int start = 0, end = num_mstr_shards;
  while (start <= end) {
    int middle = start + (end - start) / 2;
    if (mstr_shards[middle].id == id) {
      return &mstr_shards[middle];
    }
    if (mstr_shards[middle].id < id) {
      start = middle + 1;
    } else {
      end = middle - 1;
    }
  }
  return NULL;
}
