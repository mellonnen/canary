#ifndef __CANARY_CLIENT_H__
#define __CANARY_CLIENT_H__

#include "../cproto/cproto.h"
#include "../nethelpers/nethelpers.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
  char *cnf_addr;
  in_port_t cnf_port;
} CanaryCache;

CanaryCache create_canary_cache(char *, in_port_t cnf_port);

int *canary_get(CanaryCache *, char *);
void canary_put(CanaryCache *, char *, int);

#endif // __CANARY_CLIENT_H__
