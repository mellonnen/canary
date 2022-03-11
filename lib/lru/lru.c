#include "lru.h"
#include <stdlib.h>
#include <string.h>

/* ----------- HELPERS ------------------------*/

// helper that creates an entry with no dll pointers initialized
lru_entry_t *create_entry(char *key, int value) {
  lru_entry_t *entry = malloc(sizeof(lru_entry_t) * 1);
  entry->key = malloc(strlen(key) + 1);
  entry->value = value;

  strcpy(entry->key, key);
  entry->bucket_next = NULL;
  entry->lru_prev = entry->lru_next = NULL;
  return entry;
}

// helper that frees memory of an entry.
void destroy_entry(lru_entry_t *entry) {
  free(entry->key);
  free(entry);
}

// helper that employs the LRU protocol, by:
// - Disconnecting tail entry from its potential bucket.
// - Disconnecting tail entry from LRU queue and set tail pointer.
//
// It returns the pointer to the disconnected entry (the previous tail entry)
lru_entry_t *do_lru(lru_cache_t *cache) {
  lru_entry_t *remove = cache->tail; // entry to remove to free space.

  // Remove element from bucket
  if (remove->bucket_prev == NULL && remove->bucket_prev == NULL) {
    size_t slot = hash_djb2(remove->key) % cache->capacity;
    cache->entries[slot] = NULL;
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

// Helper that moves an entry to the head of the LRU queue.
void move_entry_to_head(lru_cache_t *cache, lru_entry_t *entry) {
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
/* ----------- EXTERNAL API -------------------*/

/**
 * @brief Creates an instance of an LRU cache.
 *
 * @param capacity - how many elements that can be stored in cache.
 * @return pointer to the cache struct.
 */
lru_cache_t *create_lru_cache(size_t capacity, size_t max_key_length) {
  // allocate cache pointer.
  lru_cache_t *cache = malloc(sizeof(lru_cache_t) * 1);
  cache->num_elements = 0;
  cache->capacity = capacity;
  cache->max_key_length = max_key_length;

  cache->head = cache->tail = NULL;

  cache->entries = malloc(sizeof(lru_entry_t *) * capacity);

  for (size_t i = 0; i < capacity; i++) {
    cache->entries[i] = NULL;
  }
  return cache;
}

/**
 * @brief Frees the memory of an LRU cache.
 *
 * @param cache - cache to be freed
 */
void destroy_lru_cache(lru_cache_t *cache) {

  for (size_t i = 0; i < cache->capacity; i++) {
    lru_entry_t *current = cache->entries[i];
    while (current != NULL) {
      lru_entry_t *next = current->bucket_next;
      destroy_entry(current);
      current = next;
    }
  }
  free(cache->entries);
  free(cache);
}

/**
 * @brief Will fetch (if found) the value cached to the given key.
 *
 * @param cache
 * @param key
 * @return pointer to the value, NULL means the value is not in the cache.
 */
int *get(lru_cache_t *cache, char *key) {
  size_t slot = hash_djb2(key) % cache->capacity;

  lru_entry_t *entry = cache->entries[slot];

  if (entry == NULL)
    return NULL;

  while (entry != NULL) {
    if (strcmp(entry->key, key) == 0) {
      move_entry_to_head(cache, entry);
      return &entry->value;
    }
    entry = entry->bucket_next;
  }
  return NULL;
}
/**
 * @brief Will put an key-value-pair into the cache. If the key already exists,
 * its value will be updated. NOTE: if the cache is full the least recently used
 * item will be removed.
 *
 * @param cache
 * @param key
 * @param value
 */
bool put(lru_cache_t *cache, char *key, int value) {

  // guard statement for key size
  if (strlen(key) > cache->max_key_length)
    return false;

  size_t slot = hash_djb2(key) % cache->capacity;

  lru_entry_t *entry = cache->entries[slot];

  // case for empty slot.
  if (entry == NULL) {

    if (cache->num_elements == cache->capacity) {
      // do LRU protocol and free the returned pointer.
      destroy_entry(do_lru(cache));
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
    cache->entries[slot] = entry;
    cache->num_elements++;
    return true;
  }

  // Hash collision, linear scan the bucket.
  lru_entry_t *prev;
  while (entry != NULL) {
    // Check if we already have item in cache, update value and move to head.
    if (strcmp(entry->key, key) == 0) {
      entry->value = value;
      move_entry_to_head(cache, entry);
      return true;
    }
    prev = entry;
    entry = prev->bucket_next;
  }

  // add new entry to the bucket.

  lru_entry_t *remove = NULL;
  // Free space for item if filled to capacity.
  if (cache->num_elements == cache->capacity) {
    remove = do_lru(cache); // do not free pointer immediately.
  }

  // Create and insert new element at end of bucket.
  entry = create_entry(key, value);
  entry->bucket_prev = prev;

  // in case we happend to remove the entry that `prev` is pointing to.
  if (remove == prev) {
    prev = remove->bucket_prev;
  }

  if (prev == NULL) {
    cache->entries[slot] = entry;
  } else {
    prev->bucket_next = entry;
  }

  // free removed entry;
  if (remove != NULL)
    destroy_entry(remove);

  // Insert element at head of LRU ddl.
  cache->head->lru_prev = entry;
  entry->lru_next = cache->head;
  cache->head = entry;
  cache->num_elements++;
  return true;
}
