#include "cproto.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------- EXTERNAL API  ------------------*/

// Converts 4 bytes to a Big-endian 32 bit unsigned integer.
uint32_t unpack_uint32(uint8_t bytes[4]) {
  return (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
}

/**
 * @brief Serializes a `CanaryMsg` into a array of bytes on the format
 *
 * [ msg_type | payload_len | payload ]
 * - msg_type is a unsigned 32 bit Big-endian integer.
 * - payload_len is a unsigned 32 bit Big-endian integer.
 * - payload is a number of bytes specified by payload_len
 *
 * @param msg  - CanaryMsg
 * @return a pointer to a buffer of uint8_t. NULL if there was an error during
 * memory allocation.
 */
uint8_t *serialize(CanaryMsg msg) {

  // allocate buffer on heap.
  uint8_t *buf = (uint8_t *)malloc(sizeof(msg.type) + sizeof(msg.payload_len) +
                                   msg.payload_len + 1);
  if (buf == NULL) {
    return NULL;
  }

  // Copy the data from the data into a struct.
  int curr = 0;

  // htonl = "host to network long" (in networking numbers are Big-endian)
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

/**
 * @brief Deserializes the contents of the provided buffer into a CanaryMsg
 * struct.
 *
 * @param buf - *uint8_t
 * @return pointer to a CanaryMsg struct allocated on the heap.
 */
CanaryMsg *deserialize(uint8_t *buf) {
  CanaryMsg *msg = malloc(sizeof(CanaryMsg));
  uint8_t type_buf[sizeof(CanaryMsgType)], payload_len_buf[sizeof(size_t)];

  // Copy data from tye buffer to the struct.
  int curr = 0;

  memcpy(&type_buf, buf + curr, sizeof(CanaryMsgType));
  msg->type = unpack_uint32(type_buf);
  curr += sizeof(CanaryMsgType);

  memcpy(&payload_len_buf, buf + curr, sizeof(uint32_t));
  msg->payload_len = unpack_uint32(payload_len_buf);
  curr += sizeof(uint32_t);

  msg->payload = malloc(msg->payload_len);
  memcpy(msg->payload, buf + curr, msg->payload_len);

  return msg;
}
