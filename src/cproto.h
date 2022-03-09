#ifndef __CPROTO_H__
#define __CPROTO_H__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  Error,
  // Register shards with cnf_svc
  RegisterShard2Cnf,
  RegisterCnf2Mstr,
  RegisterCnf2Flw,

  // Shard heartbeats
  HeartbeatShard2Cnf,

  // Get shard cnf_svc
  ShardInfoClient2Shard,
  ShardInfoShard2Client,

  // Get cache value from shard
  GetClient2Shard,
  GetShard2Client,

  // Put cache value in shard
  PutClient2Mstr,

  // Replicate the master shard.
  ReplicateMstr2Flwr,

  // Promote follower shard
  PromoteCnf2Mstr,
  PromoteMstr2Cnf,
  PromoteCnf2Flwr,
} CanaryMsgType;

typedef struct {
  CanaryMsgType type;
  uint32_t payload_len;
  uint8_t *payload;
} CanaryMsg;

uint32_t uint32_unpack(uint8_t *);
uint8_t *serialize(CanaryMsg);
CanaryMsg *deserialize(uint8_t *);

#endif //__CPROTO_H__
