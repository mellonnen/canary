#ifndef __NETHELPERS_H__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>

typedef struct sockaddr SA;
typedef struct sockaddr_in SA_IN;
typedef struct in_addr IA;

int connect_to_socket(char *, in_port_t);
int bind_n_listen_socket(in_port_t, int);

#endif // __NETHELPERS_H__
