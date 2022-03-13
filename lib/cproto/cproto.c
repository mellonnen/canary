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

/**
 * @brief Reads `buf_size` bytes from the provided socket into the provided
 * buffer.
 *
 * @param socket - int
 * @param buf - uint8_t *
 * @param buf_size - size_t
 * @return -1 if something went wrong.
 */
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

/**
 * @brief Writes `buf_size` bytes from the provided buffer into the provided
 * socket.
 *
 * @param socket - int
 * @param buf - uint8_t *
 * @param buf_size - size_t
 * @return -1 if something went wrong.
 */
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

/**
 * @brief Serializes a `CanaryMsg` into a buffer of bytes on the format
 *
 * [ msg_type | payload_len | payload ]
 * - msg_type is a unsigned 32 bit Big-endian integer.
 * - payload_len is a unsigned 32 bit Big-endian integer.
 * - payload is a number of bytes specified by payload_len
 *
 * NOTE: Allocates memory for the buffer on the heap.
 *
 * @param msg - CanaryMsg
 * @param buf - uint8_t **
 * @return The size of the serialized buffer, -1 if something went wrong.
 */
int serialize(CanaryMsg msg, uint8_t **buf) {

  int buf_size = sizeof(msg.type) + sizeof(msg.payload_len) + msg.payload_len;
  *buf = (uint8_t *)malloc(buf_size);
  if (*buf == NULL) {
    return -1;
  }

  int bytes_copied = 0;

  uint32_t n_type = htonl(msg.type);
  memcpy(*buf + bytes_copied, &n_type, sizeof(msg.type));
  bytes_copied += sizeof(msg.type);

  uint32_t n_payload_len = htonl(msg.payload_len);
  memcpy(*buf + bytes_copied, &n_payload_len, sizeof(msg.payload_len));
  bytes_copied += sizeof(msg.payload_len);

  memcpy(*buf + bytes_copied, msg.payload, msg.payload_len);
  bytes_copied += msg.payload_len;

  return buf_size;
}

/**
 * @brief Deserializes the contents of the provided buffer into the provided
 * CanaryMsg struct.
 *
 * NOTE: The message payload is allocated on heap.
 *
 * @param buf - *uint8_t
 * @param msg - CanaryMsg *
 * @return -1 if something went wrong, 0 otherwise.
 */
int deserialize(uint8_t *buf, CanaryMsg *msg) {

  int bytes_copied = 0;

  // Convert the to host endianess.
  msg->type = ntohl(*(CanaryMsgType *)(buf + bytes_copied));
  bytes_copied += sizeof(CanaryMsgType);

  msg->payload_len = ntohl(*(uint32_t *)(buf + bytes_copied));
  bytes_copied += sizeof(uint32_t);

  msg->payload = malloc(msg->payload_len);
  memcpy(msg->payload, buf + bytes_copied, msg->payload_len);
  return 0;
}

int pack_short(uint16_t port, uint8_t buf[2]) {
  in_port_t n_port = htons(port);
  memcpy(buf, &n_port, sizeof(n_port));
  return 0;
}

int unpack_short(uint16_t *port, uint8_t *buf) {
  *port = ntohs(*(in_port_t *)buf);
  return 0;
}

int pack_string_int(char *key, uint32_t key_len, int value, uint8_t *buf) {
  int bytes_packed = 0;

  uint32_t n_key_len = htonl(key_len);
  memcpy(buf + bytes_packed, &n_key_len, sizeof(n_key_len));
  bytes_packed += sizeof(n_key_len);

  memcpy(buf + bytes_packed, key, key_len);
  bytes_packed += key_len;

  uint32_t n_value = htonl(value);
  memcpy(buf + bytes_packed, &n_value, sizeof(n_value));
  return 0;
}

int unpack_string_int(char **key, int *value, uint8_t *buf) {
  int bytes_unpacked = 0;

  uint32_t key_len = ntohl(*(uint32_t *)(buf + bytes_unpacked));
  bytes_unpacked += sizeof(key_len);

  *key = malloc(key_len);
  memcpy(*key, buf + bytes_unpacked, key_len);
  bytes_unpacked += key_len;

  *value = ntohl(*(int *)(buf + bytes_unpacked));
  return 0;
}

int pack_string_short(char *str, uint32_t str_len, uint16_t num, uint8_t *buf) {
  int bytes_packed = 0;

  uint32_t n_str_len = htonl(str_len);
  memcpy(buf + bytes_packed, &n_str_len, sizeof(n_str_len));
  bytes_packed += sizeof(n_str_len);

  memcpy(buf + bytes_packed, str, str_len);
  bytes_packed += str_len;

  uint16_t n_num = htons(num);
  memcpy(buf + bytes_packed, &n_num, sizeof(n_num));
  return 0;
}

int unpack_string_short(char **str, uint16_t *num, uint8_t *buf) {
  int bytes_unpacked = 0;

  uint32_t key_len = ntohl(*(uint32_t *)(buf + bytes_unpacked));
  bytes_unpacked += sizeof(key_len);

  *str = malloc(key_len);
  memcpy(*str, buf + bytes_unpacked, key_len);
  bytes_unpacked += key_len;

  *num = ntohs(*(uint16_t *)(buf + bytes_unpacked));
  return 0;
}

int pack_int_int(uint32_t num1, uint32_t num2, uint8_t *buf) {
  int bytes_packed = 0;
  uint32_t n_num1 = htonl(num1), n_num2 = htonl(num2);

  memcpy(buf + bytes_packed, &n_num1, sizeof(n_num1));
  bytes_packed += sizeof(n_num1);

  memcpy(buf + bytes_packed, &n_num2, sizeof(n_num2));
  return 0;
}
int unpack_int_int(uint32_t *num1, uint32_t *num2, uint8_t *buf) {
  *num1 = ntohl(*(uint32_t *)buf);
  *num2 = ntohl(*(uint32_t *)(buf + sizeof(*num1)));
  return 0;
}

/**
 * @brief Receives a message from the provided socket and loads it into the
 * provided CanaryMsg struct.
 *
 * @param socket - int
 * @param msg - CanaryMsg *
 * @return -1 if something went wrong.
 */
int receive_msg(int socket, CanaryMsg *msg) {
  // Read the size of message.
  uint32_t msg_size;
  uint8_t *msg_size_buf = (uint8_t *)&msg_size;

  if (read_from_socket(socket, msg_size_buf, sizeof(msg_size)) == -1)
    return -1;

  msg_size = ntohl(msg_size); // convert to the endianess of the host.

  uint8_t msg_buf[msg_size];
  if (read_from_socket(socket, msg_buf, msg_size) == -1)
    return -1;
  if (deserialize(msg_buf, msg) == -1)
    return -1;

  return 0;
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

/**
 * @brief Convenience Wrapper `send_msg` for sending an error message.
 *
 * @param socket - int
 * @param error_msg - char *
 */
void send_error_msg(int socket, const char *error_msg) {
  CanaryMsg msg = {.type = Error,
                   .payload_len = strlen(error_msg) + 1,
                   .payload = (uint8_t *)error_msg};
  send_msg(socket, msg);
}
