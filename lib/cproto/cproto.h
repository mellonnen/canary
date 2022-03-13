#ifndef __CPROTO_H__
#define __CPROTO_H__
#include <bits/types/time_t.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  Error,
  // Register shards with cnf_svc
  Mstr2CnfRegister,
  Cnf2MstrRegister,
  Flw2MstrRegister,

  // Shard heartbeats
  Shard2CnfHeartbeat,

  // Get shard cnf_svc
  Client2CnfDiscover,
  Cnf2ClientDiscover,

  // Get cache value from shard
  Client2ShardGet,
  Shard2ClientGet,

  // Put cache value in shard
  Client2MstrPut,

  Flwr2MstrConnect,
  // Replicate the master shard.
  Mstr2FlwrReplicate,

  // Promote follower shard
  Cnf2FlwrPromote,
  // TODO: add message for flwr shards to become mstr
} CanaryMsgType;

typedef struct {
  CanaryMsgType type;
  uint32_t payload_len;
  uint8_t *payload;
} CanaryMsg;

int serialize(CanaryMsg, uint8_t **);
int deserialize(uint8_t *, CanaryMsg *);

int pack_short(uint16_t, uint8_t[2]);
int unpack_short(uint16_t *, uint8_t *);
int pack_string_int(char *, uint32_t, int, uint8_t *);
int unpack_string_int(char **, int *, uint8_t *);
int pack_string_short(char *, uint32_t, uint16_t, uint8_t *);
int unpack_string_short(char **, uint16_t *, uint8_t *);

int receive_msg(int, CanaryMsg *);
int send_msg(int, CanaryMsg);
void send_error_msg(int, const char *);
int compare_shards(const void *, const void *);

#endif //__CPROTO_H__
