#ifndef __CPROTO_H__
#define __CPROTO_H__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  // Register shards with cnf_svc
  register_shard2cnf,
  register_cnf2mstr,
  regiser_cnf2flw,

  // Shard heartbeats
  heartbeat_shard2cnf,

  // Get shard cnf_svc
  shardinfo_client2shard,
  shardinfo_shard2client,

  // Get cache value from shard
  get_client2shard,
  get_shard2client,

  // Put cache value in shard
  put_client2shard,
  put_shard2client,

  // Replicate the master shard.
  replicate_mstr2flwr,

  // Promote follower shard
  promote_cnf2shard,
  promote_shard2cnf
} CanaryMsgType;

typedef struct {
  CanaryMsgType type;
  uint32_t payload_len;
  uint8_t *payload;
} CanaryMsg;

uint8_t *serialize(CanaryMsg);
CanaryMsg *deserialize(uint8_t *);

#endif //__CPROTO_H__
