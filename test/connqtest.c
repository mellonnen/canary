#include "../lib/connq/connq.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void test_queue();

int main(int argc, char *argv[]) {
  printf("\nTESTS FOR QUEUE:\n\n");
  test_queue();
  return 0;
}

void test_queue() {
  printf("\tTest queue operations:\n");
  conn_queue_t q = create_queue();

  printf("\t\tTest dequeue empty queue...");
  assert(dequeue(&q) == NULL);
  printf("✅\n");

  printf("\t\ttest enqueue first element...");

  conn_ctx_t *first = malloc(sizeof(conn_ctx_t));
  *first = (conn_ctx_t){.socket = 1};

  enqueue(&q, first);
  assert(q.head == q.tail);
  assert(q.head->ctx->socket == 1);
  printf("✅\n");

  printf("\t\ttest enqueue second element...");
  conn_ctx_t *second = malloc(sizeof(conn_ctx_t));
  *second = (conn_ctx_t){.socket = 2};
  enqueue(&q, second);
  assert(q.head->ctx->socket == 1);
  assert(q.tail->ctx->socket == 2);
  printf("✅\n");

  printf("\t\ttest dequeue first element...");
  conn_ctx_t *d1 = dequeue(&q);
  assert(d1 != NULL);
  assert(d1->socket == 1);
  assert(q.head == q.tail);
  assert(q.head->ctx->socket == 2);
  printf("✅\n");

  printf("\t\ttest dequeue first element...");
  conn_ctx_t *d2 = dequeue(&q);
  assert(d2 != NULL);
  assert(d2->socket == 2);
  assert(q.head == NULL);
  assert(q.tail == NULL);
  printf("✅\n");

  free(first);
  free(second);
  destroy_queue(&q);
}
