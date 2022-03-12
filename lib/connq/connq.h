#ifndef __CANARY_QUEUE_H__
#define __CANARY_QUEUE_H__

#include "../nethelpers/nethelpers.h"
#include <stdlib.h>

typedef struct {
  int socket;
  IA client_addr;
  in_port_t port;
} conn_ctx_t;

typedef struct node {
  struct node *next;
  conn_ctx_t *ctx;
} node_t;

typedef struct {
  node_t *head;
  node_t *tail;
} conn_queue_t;

conn_queue_t create_queue();
void destroy_queue(conn_queue_t *);
void enqueue(conn_queue_t *, conn_ctx_t *);
conn_ctx_t *dequeue(conn_queue_t *);

#endif // __CANARY_QUEUE_H__
