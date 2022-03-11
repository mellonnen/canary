#include <stdlib.h>

// source: http://www.cse.yorku.ca/~oz/hash.html
size_t hash_djb2(const char *str) {
  unsigned long hash = 5831;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}
