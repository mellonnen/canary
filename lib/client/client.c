#include "client.h"

int get_shard(int socket, char *key, char **addr, in_port_t *port);
int *get_from_shard(int socket, char *key);
void put_in_shard(int socket, char *key, int value);

CanaryCache create_canary_cache(char *cnf_addr, in_port_t cnf_port) {
  return (CanaryCache){.cnf_addr = cnf_addr, .cnf_port = cnf_port};
}

int *canary_get(CanaryCache *cache, char *key) {
  int cnf_socket, shard_socket;
  in_port_t shard_port;
  char *shard_addr;

  if ((cnf_socket = connect_to_socket(cache->cnf_addr, cache->cnf_port)) == -1)
    return NULL;

  if (get_shard(cnf_socket, key, &shard_addr, &shard_port) == -1)
    return NULL;

  if ((shard_socket = connect_to_socket(shard_addr, shard_port)) == -1)
    return NULL;

  return get_from_shard(shard_socket, key);
}

void canary_put(CanaryCache *cache, char *key, int value) {
  int cnf_socket, shard_socket;
  in_port_t shard_port;
  char *shard_addr;

  if ((cnf_socket = connect_to_socket(cache->cnf_addr, cache->cnf_port)) == -1)
    return;

  if (get_shard(cnf_socket, key, &shard_addr, &shard_port) == -1)
    return;

  if ((shard_socket = connect_to_socket(shard_addr, shard_port)) == -1)
    return;
  put_in_shard(shard_socket, key, value);
}

int get_shard(int socket, char *key, char **addr, in_port_t *port) {
  CanaryMsg req, resp;

  req = (CanaryMsg){.type = Client2CnfDiscover,
                    .payload_len = strlen(key),
                    .payload = (uint8_t *)key};

  send_msg(socket, req);
  receive_msg(socket, &resp);

  if (resp.type != Cnf2ClientDiscover)
    return -1;

  unpack_string_short(addr, port, resp.payload);
  return 0;
}

int *get_from_shard(int socket, char *key) {
  CanaryMsg req, resp;

  uint32_t payload_len = strlen(key);
  req = (CanaryMsg){.type = Client2ShardGet,
                    .payload_len = payload_len,
                    .payload = (uint8_t *)key};

  send_msg(socket, req);
  receive_msg(socket, &resp);

  if (resp.type != Shard2ClientGet)
    return NULL;

  if (resp.payload_len == 0)
    return NULL;

  return (int *)resp.payload;
}
void put_in_shard(int socket, char *key, int value) {
  uint32_t key_len = strlen(key);
  size_t payload_len = sizeof(key_len) + key_len + sizeof(value);

  uint8_t *payload = malloc(payload_len);
  pack_string_int(key, key_len, value, payload);

  CanaryMsg msg = {
      .type = Client2MstrPut, .payload_len = payload_len, .payload = payload};

  send_msg(socket, msg);
  free(payload);
}
