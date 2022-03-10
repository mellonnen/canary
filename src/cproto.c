#include "cproto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ----------- HELPERS ------------------------*/

int read_from_socket(int socket, uint8_t *buf, size_t buf_size) {
  int rc, bytes_read = 0;
  do {
    rc = read(socket, buf + bytes_read, buf_size - bytes_read);
    if (rc <= 0) {
      if ((errno == EAGAIN || errno == EWOULDBLOCK))
        continue;

      if (errno != EINTR)
        return -1;
    } else {
      bytes_read += rc;
    }
  } while (bytes_read < buf_size);
  return 0;
}

int write_to_socket(int socket, uint8_t *buf, size_t buf_size) {
  int rc, bytes_written = 0;

  do {
    rc = write(socket, buf + bytes_written, buf_size - bytes_written);
    if (rc <= 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue;

      if (errno != EINTR)
        return -1;
    } else {
      bytes_written += rc;
    }
  } while (bytes_written < buf_size);
  return 0;
}

/* ----------- EXTERNAL API  ------------------*/

// Converts 4 bytes to a Big-endian 32 bit unsigned integer.
uint32_t unpack_uint32(uint8_t bytes[4]) {
  return (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
}

/**
 * @brief Serializes a `CanaryMsg` into a buffer of bytes on the format
 *
 * [ msg_type | payload_len | payload ]
 * - msg_type is a unsigned 32 bit Big-endian integer.
 * - payload_len is a unsigned 32 bit Big-endian integer.
 * - payload is a number of bytes specified by payload_len
 *
 * @param msg - CanaryMsg
 * @param buf - **buf
 * @return The size of the serialized buffer, -1 if something went wrong.
 */
int serialize(CanaryMsg msg, uint8_t **buf) {

  // allocate buffer on heap.
  int buf_size = sizeof(msg.type) + sizeof(msg.payload_len) + msg.payload_len;
  *buf = (uint8_t *)malloc(buf_size);
  if (*buf == NULL) {
    return -1;
  }

  // Copy the data from the data into a struct.
  int curr = 0;

  // htonl = "host to network long" (in networking numbers are Big-endian)
  uint32_t n_type = htonl(msg.type);
  memcpy(*buf + curr, &n_type, sizeof(msg.type));
  curr += sizeof(msg.type);

  uint32_t n_payload_len = htonl(msg.payload_len);
  memcpy(*buf + curr, &n_payload_len, sizeof(msg.payload_len));
  curr += sizeof(msg.payload_len);

  memcpy(*buf + curr, msg.payload, msg.payload_len);
  curr += msg.payload_len;

  return buf_size;
}

/**
 * @brief Deserializes the contents of the provided buffer into the provided
 * CanaryMsg struct.
 *
 * @param buf - *uint8_t
 * @param msg - *CanaryMsg
 * @return -1 if something went wrong, 0 otherwise.
 */
int deserialize(uint8_t *buf, CanaryMsg *msg) {
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
  return 0;
}

CanaryMsg *receive_msg(int socket) {
  // Read the size of message.
  uint32_t msg_size;
  uint8_t *msg_size_buf = (uint8_t *)&msg_size;

  if (read_from_socket(socket, msg_size_buf, sizeof(msg_size)) == -1)
    return NULL;

  msg_size = ntohl(msg_size); // convert to the endianess of the host.

  uint8_t msg_buf[msg_size];
  if (read_from_socket(socket, msg_buf, msg_size) == -1)
    return NULL;
  CanaryMsg *msg = malloc(sizeof(CanaryMsg *));
  if (deserialize(msg_buf, msg) == -1)
    return NULL;

  return msg;
}

/**
 * @brief sends the provided CanaryMsg over the socket
 *
 * @param socket - int
 * @param msg - CanaryMsg
 * @return -1 if something went wrong.
 */
int send_msg(int socket, CanaryMsg msg) {
  uint8_t *msg_buf;
  int msg_size = serialize(msg, &msg_buf);

  if (msg_size == -1) {
    return -1;
  }
  msg_size = htonl(msg_size);
  uint8_t *msg_size_buf = (uint8_t *)&msg_size;

  if (write_to_socket(socket, msg_size_buf, sizeof(msg_size)) == -1)
    return -1;

  if (write_to_socket(socket, msg_buf, msg_size) == -1)
    return -1;

  free(msg_buf);

  return 0;
}
