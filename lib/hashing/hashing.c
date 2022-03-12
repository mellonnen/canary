#include <stdint.h>
#include <stdlib.h>

// source: http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash_djb2(const char *str) {
  unsigned long hash = 5831;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}

#if RAND_MAX / 256 >= 0xFFFFFFFFFFFFFF
#define LOOP_COUNT 1
#elif RAND_MAX / 256 >= 0xFFFFFF
#define LOOP_COUNT 2
#elif RAND_MAX / 256 >= 0x3FFFF
#define LOOP_COUNT 3
#elif RAND_MAX / 256 >= 0x1FF
#define LOOP_COUNT 4
#else
#define LOOP_COUNT 5
#endif

unsigned long rand64() {
  unsigned long r = 0;
  for (int i = LOOP_COUNT; i > 0; i--) {
    r = r * (RAND_MAX + (unsigned long)1) + rand();
  }
  return r;
}
