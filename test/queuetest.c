#include "../lib/queue/queue.h"
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
  queue_t q = create_queue();

  printf("\t\tTest dequeue empty queue...");
  assert(dequeue(&q) == NULL);
  printf("✅\n");

  printf("\t\ttest enqueue first element...");

  int *first = malloc(sizeof(int));
  *first = 1;

  enqueue(&q, first);
  assert(q.head == q.tail);
  assert(*q.head->client_socket == 1);
  printf("✅\n");

  printf("\t\ttest enqueue second element...");
  int *second = malloc(sizeof(int));
  *second = 2;
  enqueue(&q, second);
  assert(*q.head->client_socket == 1);
  assert(*q.tail->client_socket == 2);
  printf("✅\n");

  printf("\t\ttest dequeue first element...");
  int *d1 = dequeue(&q);
  assert(d1 != NULL);
  assert(*d1 == 1);
  assert(q.head == q.tail);
  assert(*q.head->client_socket == 2);
  printf("✅\n");

  printf("\t\ttest dequeue first element...");
  int *d2 = dequeue(&q);
  assert(d2 != NULL);
  assert(*d2 == 2);
  assert(q.head == NULL);
  assert(q.tail == NULL);
  printf("✅\n");

  free(first);
  free(second);
  destroy_queue(&q);
}
