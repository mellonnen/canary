#include "lru.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_put() {
  lru_cache_t *cache = create_lru_cache(3);

  put(cache, "limp", 1);
  put(cache, "limpz", 2);
  put(cache, "limpan", 2);

  printf("\ttest num elements...");
  assert(cache->num_elements == 3);
  printf("✅\n");

  printf("\ttest head entry is correct...");
  assert(strcmp(cache->head->key, "limpan") == 0);
  assert(cache->head->lru_prev == NULL);
  printf("✅\n");

  printf("\ttest tail entry is correct...");
  assert(strcmp(cache->tail->key, "limp") == 0);
  assert(cache->tail->lru_next == NULL);
  printf("✅\n");

  printf("\ttest LRU queue is correct...");
  assert(cache->head->lru_next == cache->tail->lru_prev);
  assert(strcmp(cache->head->lru_next->key, "limpz") == 0);
  printf("✅\n");

  put(cache, "limpan", 3);
  printf("\ttest updating head value...");
  assert(strcmp(cache->head->key, "limpan") == 0);
  assert(cache->head->value == 3);
  printf("✅\n");

  put(cache, "limp", 3);
  printf("\ttest updating entry in middle of LRU queue...");
  assert(strcmp(cache->head->key, "limp") == 0);
  assert(cache->head->value == 3);
  printf("✅\n");

  printf("\ttest updating tail entry...");
  put(cache, "limpz", 3);
  assert(strcmp(cache->head->key, "limpz") == 0);
  assert(cache->head->value == 3);
  printf("✅\n");

  put(cache, "limpzy", 3);
  printf("\ttest inserting into full cache...");
  assert(strcmp(cache->head->key, "limpzy") == 0);
  assert(strcmp(cache->tail->key, "limp") == 0);
  printf("✅\n");

  destroy_lru_cache(cache);
}

void test_get() {
  lru_cache_t *cache = create_lru_cache(3);

  put(cache, "limp", 1);
  put(cache, "limpz", 2);
  put(cache, "limpan", 2);

  int *val;

  val = get(cache, "limp");
  printf("\ttest get head entry...");
  assert(strcmp(cache->head->key, "limp") == 0);
  assert(*val == 1);
  printf("✅\n");

  val = get(cache, "limpz");
  printf("\ttest get entry in middle of LRU queue...");
  assert(strcmp(cache->head->key, "limpz") == 0);
  assert(strcmp(cache->head->lru_next->key, "limp") == 0);
  assert(*val == 2);
  printf("✅\n");

  val = get(cache, "limpan");
  printf("\ttest get tail entry...");
  assert(strcmp(cache->head->key, "limpan") == 0);
  assert(strcmp(cache->tail->key, "limp") == 0);
  assert(*val == 2);
  printf("✅\n");
}

int main(int argc, char *argv[]) {
  printf("Testing put:\n");
  test_put();
  printf("Testing get:\n");
  test_get();
  return 0;
}
