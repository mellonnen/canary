#ifndef __LOGGER_H__
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
int logfmt(const char *format, ...);
#endif /* __LOGGER_H__ */
