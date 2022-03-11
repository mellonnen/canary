#ifndef __CPROTO_H__
#define __CPROTO_H__
#include <netinet/in.h>
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

typedef struct {
  size_t id;
  struct in_addr ip;
  in_port_t port;
} CanaryShardInfo;

int serialize(CanaryMsg, uint8_t **);
int deserialize(uint8_t *, CanaryMsg *);
int pack_register_payload(in_port_t, uint8_t[2]);
int unpack_register_payload(in_port_t *, uint8_t *);

int receive_msg(int, CanaryMsg *);
int send_msg(int, CanaryMsg);
void send_error_msg(int, const char *);
int compare_shards(const void *, const void *);

#endif //__CPROTO_H__
