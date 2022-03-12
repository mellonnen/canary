#ifndef __CANARY_QUEUE_H__
#define __CANARY_QUEUE_H__

#include <stdlib.h>

typedef struct node {
  struct node *next;
  int *client_socket;
} node_t;

typedef struct {
  node_t *head;
  node_t *tail;
} queue_t;

queue_t create_queue();
void destroy_queue(queue_t *);
void enqueue(queue_t *, int *);
int *dequeue(queue_t *);

#endif // __CANARY_QUEUE_H__
