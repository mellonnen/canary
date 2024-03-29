#include "../lib/lru/lru.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_put() {
  lru_cache_t *cache = create_lru_cache(3);
  lru_entry_t *removed;
  printf("\t\ttest initial put...");
  removed = put(cache, "limp", 1);
  assert(removed == NULL);
  removed = put(cache, "limpz", 2);
  assert(removed == NULL);
  removed = put(cache, "limpan", 2);
  assert(removed == NULL);
  printf("✅\n");

  printf("\t\ttest num elements...");
  assert(cache->num_elements == 3);
  printf("✅\n");

  printf("\t\ttest head entry is correct...");
  assert(strcmp(cache->head->key, "limpan") == 0);
  assert(cache->head->lru_prev == NULL);
  printf("✅\n");

  printf("\t\ttest tail entry is correct...");
  assert(strcmp(cache->tail->key, "limp") == 0);
  assert(cache->tail->lru_next == NULL);
  printf("✅\n");

  printf("\t\ttest LRU queue is correct...");
  assert(cache->head->lru_next == cache->tail->lru_prev);
  assert(strcmp(cache->head->lru_next->key, "limpz") == 0);
  printf("✅\n");

  printf("\t\ttest updating head value...");
  removed = put(cache, "limpan", 3);
  assert(removed == NULL);
  assert(strcmp(cache->head->key, "limpan") == 0);
  assert(cache->head->value == 3);
  printf("✅\n");

  printf("\t\ttest updating entry in middle of LRU queue...");
  removed = put(cache, "limp", 3);
  assert(removed == NULL);
  assert(strcmp(cache->head->key, "limp") == 0);
  assert(cache->head->value == 3);
  printf("✅\n");

  printf("\t\test updating tail entry...");
  removed = put(cache, "limpz", 3);
  assert(removed == NULL);
  assert(strcmp(cache->head->key, "limpz") == 0);
  assert(cache->head->value == 3);
  printf("✅\n");

  printf("\t\ttest inserting into full cache...");
  removed = put(cache, "limpzy", 3);
  assert(removed != NULL);
  assert(strcmp(removed->key, "limpan") == 0);
  assert(strcmp(cache->head->key, "limpzy") == 0);
  assert(strcmp(cache->tail->key, "limp") == 0);
  printf("✅\n");

  destroy_entry(removed);
  destroy_lru_cache(cache);
}

void test_get() {
  lru_cache_t *cache = create_lru_cache(3);

  put(cache, "limp", 1);
  put(cache, "limpz", 2);
  put(cache, "limpan", 2);

  int *val;

  val = get(cache, "limp");
  printf("\t\ttest get head entry...");
  assert(strcmp(cache->head->key, "limp") == 0);
  assert(*val == 1);
  printf("✅\n");

  val = get(cache, "limpz");
  printf("\t\ttest get entry in middle of LRU queue...");
  assert(strcmp(cache->head->key, "limpz") == 0);
  assert(strcmp(cache->head->lru_next->key, "limp") == 0);
  assert(*val == 2);
  printf("✅\n");

  val = get(cache, "limpan");
  printf("\t\ttest get tail entry...");
  assert(strcmp(cache->head->key, "limpan") == 0);
  assert(strcmp(cache->tail->key, "limp") == 0);
  assert(*val == 2);
  printf("✅\n");

  val = get(cache, "limper");
  printf("\t\ttest get entry not in cache...");
  assert(val == NULL);
  printf("✅\n");

  destroy_lru_cache(cache);
}

int main(int argc, char *argv[]) {
  printf("\nTEST FOR LRU CACHE:\n\n");
  printf("\tTesting put:\n");
  test_put();
  printf("\n");
  printf("\tTesting get:\n");
  test_get();
  return 0;
}
