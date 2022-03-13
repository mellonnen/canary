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

#define MAX_MASTER_SHARDS 100
#define MAX_FLWR_PER_MASTER 2

#define MAXTHREADS 10
#define HEARTBEAT_INTERVAL_WITH_SLACK 15
#define SHARD_MAITNENANCE_INTERVAL 20

typedef struct {
  IA addr;
  in_port_t port;
} shard_t;

typedef struct {
  shard_t shard;
  time_t expiration;
  bool expired;
} follower_shard_t;

typedef struct {
  uint32_t id;
  shard_t shard;
  time_t expiration;
  bool expired;
  int num_flwrs;
  follower_shard_t *flwrs[MAX_FLWR_PER_MASTER];
} master_shard_t;

master_shard_t mstr_shards[MAX_MASTER_SHARDS];
pthread_rwlock_t shards_lock;

int num_mstr_shards = 0;
int max_mstr_shards = MAX_MASTER_SHARDS;
int max_flwr_per_master = MAX_FLWR_PER_MASTER;
int flwr_per_master = 0;

pthread_t thread_pool[MAXTHREADS], shard_maitnenance;

conn_queue_t conn_q;
pthread_cond_t conn_q_cond;
pthread_mutex_t conn_q_lock;

int run(in_port_t port);
void *worker_thread(void *arg);
void *shard_maintenance_thread(void *arg);
void handle_connection(conn_ctx_t *ctx);
void handle_master_shard_registration(int socket, uint8_t *payload, IA addr);
void handle_flwr_shard_registration(int socket, uint8_t *payload, IA addr);

void handle_shard_selection(int socket, uint8_t *payload);
void handle_shard_heartbeat(uint8_t *payload);

int compare_shards(const void *a, const void *b);

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
  pthread_create(&shard_maitnenance, NULL, shard_maintenance_thread, NULL);
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

void *shard_maintenance_thread(void *arg) {
  while (1) {
    sleep(SHARD_MAITNENANCE_INTERVAL);
    // BEGIN CRITICAL SECTION
    pthread_rwlock_wrlock(&shards_lock);
    if (num_mstr_shards > 0) {
      // Scan through the shards and mark expired shards.
      int num_expired = 0;
      for (int i = 0; i < num_mstr_shards; i++) {
        if (mstr_shards[i].expiration < time(NULL)) {
          mstr_shards[i].expired = true;
          num_expired++;
          printf("Shard at %s:%d has expired\n",
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

void handle_connection(conn_ctx_t *ctx) {
  int socket = ctx->socket;
  IA client_addr = ctx->client_addr;
  CanaryMsg msg;

  free(ctx);

  printf("monkey\n");

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
    handle_shard_selection(socket, msg.payload);
    break;
  case Shard2CnfHeartbeat:
    handle_shard_heartbeat(msg.payload);
    break;
  default:
    send_error_msg(socket, "Incorrect Canary message type");
    break;
  }
}

void handle_master_shard_registration(int socket, uint8_t *payload, IA addr) {
  in_port_t port;
  unpack_short(&port, payload);
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
    printf("Could not register shard at %s:%d\n", inet_ntoa(addr), port);
    send_error_msg(socket, "Reached max shard capacity");
    return;
  }
  mstr_shards[num_mstr_shards] = mstr;
  num_mstr_shards++;
  qsort(mstr_shards, num_mstr_shards, sizeof(master_shard_t), compare_shards);
  pthread_rwlock_unlock(&shards_lock);
  // CRITICAL SECTION END

  printf("Registered shard : {\n\tid: %d,\n\tip: %s\n\tport: %d\n}\n", mstr.id,
         inet_ntoa(mstr.shard.addr), mstr.shard.port);

  uint32_t n_id = htonl(id);
  send_msg(socket, (CanaryMsg){.type = Cnf2MstrRegister,
                               .payload_len = sizeof(uint32_t),
                               .payload = (uint8_t *)&n_id});
}

void handle_flwr_shard_registration(int socket, uint8_t *payload, IA addr) {
  if (flwr_per_master >= max_flwr_per_master) {
    send_error_msg(socket, "max capacity for follower shards reached");
    return;
  }

  in_port_t port;
  unpack_short(&port, payload);

  follower_shard_t *flwr = malloc(sizeof(follower_shard_t));

  flwr->shard = (shard_t){.addr = addr, .port = port};
  flwr->expired = false;

  int mstr_idx = -1, flwr_idx = -1;
  // START CRITICAL SECTION
  pthread_rwlock_wrlock(&shards_lock);
  // Loop over the master shards, to distribute follower shards evenly.

  // EXAMPLE:  If we want a max of 2 followers per shard, we make sure that all
  // active master shard has at least 1 follower before we start we assign a
  // second follower to master shards.
  for (int i = 0; i < num_mstr_shards; i++) {
    // Check if this shard already has enough followers.
    if (mstr_shards[i].num_flwrs >= max_flwr_per_master)
      continue;

    // We are done with this "round".
    if (i == num_mstr_shards - 1)
      flwr_per_master++;

    // Find the empty follower slot.
    for (int j = 0; j < max_flwr_per_master; j++) {
      if (mstr_shards[i].flwrs[j] != NULL)
        continue;

      flwr->expiration = time(NULL) + HEARTBEAT_INTERVAL_WITH_SLACK;
      mstr_shards[i].flwrs[j] = flwr;
      mstr_idx = i;
      flwr_idx = j;
      mstr_shards[i].num_flwrs++;
      break;
    }
  }
  pthread_rwlock_unlock(&shards_lock);
  // END CRITICAL SECTION

  if (mstr_idx == -1 || flwr_idx == -1) {
    send_error_msg(socket, "Could not register shard as follower");
    return;
  }

  char *mstr_addr = inet_ntoa(mstr_shards[mstr_idx].shard.addr);
  in_port_t mstr_port = mstr_shards[mstr_idx].shard.port;

  int buf_len = sizeof(mstr_idx) + sizeof(flwr_idx) + sizeof(uint32_t) +
                strlen(mstr_addr) + 1 + sizeof(mstr_port);
  uint8_t *buf = malloc(buf_len);

  // pack indexes.
  pack_int_int(mstr_idx, flwr_idx, buf);
  // pack mstr addr and port.
  pack_string_short(mstr_addr, strlen(mstr_addr) + 1, mstr_port,
                    (buf + sizeof(mstr_idx) + sizeof(flwr_idx)));

  send_msg(socket, (CanaryMsg){.type = Cnf2FlwrRegister,
                               .payload_len = buf_len,
                               .payload = buf});
}

void handle_shard_selection(int socket, uint8_t *payload) {
  // cast payload to string and hash it.
  char *key = (char *)payload;
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

  // Pack the payload and send the message to client.
  char *addr = inet_ntoa(mstr_shards[idx].shard.addr);
  in_port_t port = mstr_shards[idx].shard.port;
  uint32_t addr_len = strlen(addr) + 1;
  uint32_t buf_len = sizeof(addr_len) + addr_len + sizeof(port);
  uint8_t *buf = malloc(buf_len);

  pack_string_short(addr, addr_len, port, buf);

  CanaryMsg msg = {
      .type = Cnf2ClientDiscover, .payload_len = buf_len, .payload = buf};

  send_msg(socket, msg);
  printf(
      "Notified that shard at %s:%d has responsibility of key %s to client\n",
      addr, port, key);
}

void handle_shard_heartbeat(uint8_t *payload) {
  uint32_t id = ntohl(*(uint32_t *)payload);
  // BEGIN CRITICAL SECTION
  pthread_rwlock_wrlock(&shards_lock);
  int start = 0, end = num_mstr_shards;
  while (start <= end) {
    int middle = start + (end - start) / 2;
    if (mstr_shards[middle].id == id) {
      mstr_shards[middle].expiration =
          time(NULL) + HEARTBEAT_INTERVAL_WITH_SLACK;
      break;
    }
    if (mstr_shards[middle].id < id) {
      start = middle + 1;
    } else {
      end = middle - 1;
    }
  }
  pthread_rwlock_unlock(&shards_lock);
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
