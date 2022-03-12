#include "queue.h"

queue_t create_queue() { return (queue_t){.head = NULL, .tail = NULL}; }

void destroy_queue(queue_t *q) {
  if (q->head == NULL)
    return;

  node_t *next, *curr = q->head;
  do {
    next = curr->next;
    free(curr);
    curr = next;
  } while (curr != NULL);
}

void enqueue(queue_t *q, int *client_socket) {
  node_t *new_node = malloc(sizeof(node_t));
  new_node->client_socket = client_socket;
  new_node->next = NULL;
  if (q->tail == NULL) {
    q->head = new_node;
  } else {
    q->tail->next = new_node;
  }
  q->tail = new_node;
}

int *dequeue(queue_t *q) {
  if (q->head == NULL)
    return NULL;

  int *res = q->head->client_socket;
  node_t *temp = q->head;
  q->head = q->head->next;
  if (q->head == NULL)
    q->tail = NULL;
  free(temp);
  return res;
}
