#include "cproto.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t big_endian_32(uint8_t bytes[4]) {
  return (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
}

uint8_t *serialize(CanaryMsg msg) {

  uint8_t *buf = (uint8_t *)malloc(sizeof(msg.type) + sizeof(msg.payload_len) +
                                   msg.payload_len + 1);
  if (buf == NULL) {
    return NULL;
  }

  int curr = 0;
  uint32_t n_type = htonl(msg.type);
  memcpy(buf + curr, &n_type, sizeof(msg.type));
  curr += sizeof(msg.type);

  uint32_t n_payload_len = htonl(msg.payload_len);
  memcpy(buf + curr, &n_payload_len, sizeof(msg.payload_len));
  curr += sizeof(msg.payload_len);

  memcpy(buf + curr, msg.payload, msg.payload_len);
  curr += msg.payload_len;

  memcpy(buf + curr, "\n", 1);
  return buf;
}

CanaryMsg *deserialize(uint8_t *buf) {
  CanaryMsg *msg = malloc(sizeof(CanaryMsg));
  uint8_t type_buf[sizeof(CanaryMsgType)], payload_len_buf[sizeof(size_t)];
  int curr = 0;

  memcpy(&type_buf, buf + curr, sizeof(CanaryMsgType));
  msg->type = big_endian_32(type_buf);
  curr += sizeof(CanaryMsgType);

  memcpy(&payload_len_buf, buf + curr, sizeof(uint32_t));
  msg->payload_len = big_endian_32(payload_len_buf);
  curr += sizeof(uint32_t);

  msg->payload = malloc(msg->payload_len);
  memcpy(msg->payload, buf + curr, msg->payload_len);

  return msg;
}
