#include "../lib/cproto/cproto.h"
#include <assert.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_msg_serialization();
void test_payload_packing();

int main(int argc, char *argv[]) {
  printf("\nTESTS FOR CANARY PROTOCOL HELPERS\n\n");

  printf("\tTesting serialization/deserialization of Canary Messages\n");
  test_msg_serialization();
  printf("\n");

  printf("\tTesting payload packing/unpacking\n");
  test_payload_packing();
  printf("\n");

  return 0;
}

void test_msg_serialization() {
  CanaryMsg msg, msg2;
  char *payload = "this is a payload";
  uint8_t *buf;
  msg.type = Cnf2MstrRegister;
  msg.payload = (uint8_t *)payload;
  msg.payload_len = strlen(payload) + 1;

  printf("\t\tTest serialized and deserialized structs match...");
  serialize(msg, &buf);

  deserialize(buf, &msg2);
  assert(msg.type == msg2.type);
  assert(msg.payload_len == msg2.payload_len);
  assert(memcmp(msg.payload, msg2.payload, msg.payload_len) == 0);

  printf("✅\n");
}

void test_payload_packing() {
  uint8_t short_buf[2];
  uint16_t port1 = 8080, port2;

  printf("\t\tTest short  packing/unpacking...");
  pack_short(port1, short_buf);
  unpack_short(&port2, short_buf);
  assert(port1 == port2);
  printf("✅\n");

  char *key1 = "limpan", *key2;
  uint32_t key_len = strlen(key1) + 1;
  int value1 = 15, value2;
  uint8_t *string_int_buf = malloc(sizeof(key_len) + key_len + sizeof(value1));

  printf("\t\tTest string-int packing/unpacking...");
  pack_string_int(key1, key_len, value1, string_int_buf);
  unpack_string_int(&key2, &value2, string_int_buf);
  assert(strcmp(key1, key2) == 0);
  assert(value1 == value2);
  printf("✅\n");
  free(string_int_buf);

  char *addr1 = "127.0.0.1", *addr2;
  uint32_t addr_len = strlen("127.0.0.1") + 1;
  port1 = 8080, port2 = 0;
  uint8_t *string_short_buf =
      malloc(sizeof(addr_len) + addr_len + sizeof(port1));

  printf("\t\tTest string-short packing/unpacking...");
  pack_string_short(addr1, addr_len, port1, string_short_buf);
  unpack_string_short(&addr2, &port2, string_short_buf);
  assert(strcmp(addr1, addr2) == 0);
  assert(port2 == port2);
  printf("✅\n");
  free(string_short_buf);

  uint8_t int_int_buf[8];
  uint32_t num1 = 1, num2 = 2, num3, num4;
  printf("\t\tTest int-int packing/unpacking...");
  pack_int_int(num1, num2, int_int_buf);
  unpack_int_int(&num3, &num4, int_int_buf);
  assert(num1 == num3);
  assert(num2 == num4);
  printf("✅\n");
}
