#ifndef __NETHELPERS_H__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>

typedef struct sockaddr SA;
int connect_to_socket(char *addr, in_port_t port);

#endif // __NETHELPERS_H__
