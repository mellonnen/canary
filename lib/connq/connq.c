#include "connq.h"

conn_queue_t create_queue() {
  return (conn_queue_t){.head = NULL, .tail = NULL};
}

void destroy_queue(conn_queue_t *q) {
  if (q->head == NULL)
    return;

  node_t *next, *curr = q->head;
  do {
    next = curr->next;
    free(curr);
    curr = next;
  } while (curr != NULL);
}

void enqueue(conn_queue_t *q, conn_ctx_t *ctx) {
  node_t *new_node = malloc(sizeof(node_t));
  new_node->ctx = ctx;
  new_node->next = NULL;
  if (q->tail == NULL) {
    q->head = new_node;
  } else {
    q->tail->next = new_node;
  }
  q->tail = new_node;
}

conn_ctx_t *dequeue(conn_queue_t *q) {
  if (q->head == NULL)
    return NULL;

  conn_ctx_t *ctx = q->head->ctx;
  node_t *temp = q->head;
  q->head = q->head->next;
  if (q->head == NULL)
    q->tail = NULL;
  free(temp);
  return ctx;
}
