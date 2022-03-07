#include "lru.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------- HELPERS ------------------------*/

// source: http://www.cse.yorku.ca/~oz/hash.html
size_t hash_djb2(const char *str) {
  unsigned long hash = 5831;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}

lru_entry_t *create_entry(char *key, int value) {
  lru_entry_t *entry = malloc(sizeof(lru_entry_t) * 1);
  entry->key = malloc(strlen(key) + 1);
  entry->value = value;

  strcpy(entry->key, key);
  entry->bucket_next = NULL;
  entry->lru_prev = entry->lru_next = NULL;
  return entry;
}

void destroy_entry(lru_entry_t *entry) {
  if (entry->bucket_prev != NULL)
    entry->bucket_prev->bucket_next = entry->bucket_next;

  if (entry->bucket_next != NULL)
    entry->bucket_prev->bucket_next = entry->bucket_prev;

  free(entry->key);
  free(entry);
}

void to_string(lru_cache_t *cache) {
  printf("Map:\n");
  for (int i = 0; i < cache->capacity; i++) {
    lru_entry_t *entry = cache->table[i];

    if (entry == NULL) {
      continue;
    }
    printf("slot[%4d]: ", i);

    while (1) {
      printf("%s=%d ", entry->key, entry->value);

      if (entry->bucket_next == NULL)
        break;

      entry = entry->bucket_next;
    }
    printf("\n");
  }
  printf("LRU queue:\n");

  lru_entry_t *entry = cache->head;
  while (1) {
    printf("%s ", entry->key);
    if (entry->lru_next == NULL)
      break;
    printf("- ");
    entry = entry->lru_next;
  }
  printf("\n");

  printf("Head = %s\n", cache->head->key);
  printf("Tail = %s\n", cache->tail->key);
  printf("\n");
}

lru_entry_t *do_lru(lru_cache_t *cache) {
  lru_entry_t *remove = cache->tail; // entry to remove to free space.

  // Remove element from bucket
  if (remove->bucket_prev == NULL && remove->bucket_prev == NULL) {
    size_t slot = hash_djb2(remove->key) % cache->capacity;
    cache->table[slot] = NULL;
  }

  // Update previous bucket entry link.
  if (remove->bucket_prev != NULL) {
    remove->bucket_prev->bucket_next = remove->bucket_next;
  }

  // Update next bucket entry link.
  if (remove->bucket_next != NULL) {
    remove->bucket_next->bucket_prev = remove->bucket_prev;
  }

  // Update tail pointer and free memory
  cache->tail = remove->lru_prev;
  cache->tail->lru_next = NULL;
  cache->num_elements--;
  return remove;
}

/* ----------- EXTERNAL API -------------------*/

lru_cache_t *create_lru_cache(size_t capacity) {
  // allocate cache pointer.
  lru_cache_t *cache = malloc(sizeof(lru_cache_t) * 1);
  cache->num_elements = 0;
  cache->capacity = capacity;

  cache->head = cache->tail = NULL;

  cache->table = malloc(sizeof(lru_entry_t *) * capacity);

  for (size_t i = 0; i < capacity; i++) {
    cache->table[i] = NULL;
  }
  return cache;
}

void destroy_lru_cache(lru_cache_t *cache) {
  free(cache->table);
  free(cache);
}

int *get(lru_cache_t *cache, char *key) {}

void put(lru_cache_t *cache, char *key, int value) {
  size_t slot = hash_djb2(key) % cache->capacity;

  lru_entry_t *entry = cache->table[slot];

  // case for empty slot.
  if (entry == NULL) {

    if (cache->num_elements == cache->capacity) {
      free(do_lru(cache));
    }

    entry = create_entry(key, value);

    // Put new element at head of LRU dll.
    if (cache->head != NULL) {
      cache->head->lru_prev = entry;
    }

    entry->lru_next = cache->head;
    cache->head = entry;

    // case when for first put.
    if (cache->tail == NULL)
      cache->tail = entry;

    // insert into slot.
    cache->table[slot] = entry;
    cache->num_elements++;
    return;
  }

  // Hash collsion, linear scan the bucket.
  lru_entry_t *prev;
  while (entry != NULL) {
    // Check if we already have item in cache.
    if (strcmp(entry->key, key) == 0) {
      entry->value = value;

      if (entry == cache->head)
        return;

      if (entry == cache->tail)
        cache->tail = entry->lru_prev;

      // Disconnect entry from LRU dll.
      if (entry->lru_prev != NULL) {
        entry->lru_prev->lru_next = entry->lru_next;
      }

      if (entry->lru_next != NULL) {
        entry->lru_next->lru_prev = entry->lru_prev;
      }

      // move entry to head of dll.
      entry->lru_prev = NULL;
      cache->head->lru_prev = entry;
      entry->lru_next = cache->head;
      cache->head = entry;
      return;
    }
    prev = entry;
    entry = prev->bucket_next;
  }

  lru_entry_t *remove = NULL;
  // Free space for item if filled to capacity.
  if (cache->num_elements == cache->capacity) {
    remove = do_lru(cache);
  }

  // Create and insert new element at end of bucket.
  entry = create_entry(key, value);
  entry->bucket_prev = prev;

  // in case we happend to remove the entry that `prev` is pointing to.
  if (remove == prev) {
    prev = remove->bucket_prev;
  }

  if (prev == NULL) {
    cache->table[slot] = entry;
  } else {
    prev->bucket_next = entry;
  }

  if (remove != NULL)
    free(remove);
  // Insert element at head of LRU ddl.
  cache->head->lru_prev = entry;
  entry->lru_next = cache->head;
  cache->head = entry;
  cache->num_elements++;
}
