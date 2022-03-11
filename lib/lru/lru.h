#ifndef __LRU_H__
#define __LRU_H__

#include "../hashing/hashing.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct lru_entry_t {
  char *key;
  int value;

  // hashtable bucket ll.
  struct lru_entry_t *bucket_next, *bucket_prev;

  // dll for LRU ordering
  struct lru_entry_t *lru_next, *lru_prev;

} lru_entry_t;

typedef struct {
  // hashtable slots.
  lru_entry_t **entries;
  // Cache capacity
  size_t capacity;

  size_t num_elements;
  lru_entry_t *head, *tail;
} lru_cache_t;

lru_cache_t *create_lru_cache(size_t);
void destroy_entry(lru_entry_t *entry);
void destroy_lru_cache(lru_cache_t *);

int *get(lru_cache_t *, char *);
lru_entry_t *put(lru_cache_t *, char *, int);

#endif // __LRU_H__
