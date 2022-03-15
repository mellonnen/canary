#include "logger.h"
int logfmt(const char *format, ...) {
  time_t now;
  time(&now);
  fprintf(stderr, "{\"time\":\"%s\",\"tid\":%ld,\"message\":\"",
          strtok(ctime(&now), "\n"), pthread_self());
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\"}\n");
  return 1;
}
