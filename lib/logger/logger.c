#include "logger.h"

/**
 * @brief wraps printf with timestamp and thread_id info. Logs json object to
 * stderr.
 *
 * @param format - char *
 * @return
 */
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
