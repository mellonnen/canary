#include "../lib/cproto/cproto.h"
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_msg_serialization();
void test_payload_packing();
void test_compare_shards();

int main(int argc, char *argv[]) {
  printf("\nTESTS FOR CANARY PROTOCOL HELPERS\n\n");

  printf("\tTesting serialization/deserialization of Canary Messages\n");
  test_msg_serialization();
  printf("\n");

  printf("\tTesting payload packing/unpacking\n");
  test_payload_packing();
  printf("\n");

  printf("\tTesting shard info comparator\n");
  test_compare_shards();
  printf("\n");

  return 0;
}

void test_msg_serialization() {
  CanaryMsg msg, msg2;
  char *payload = "this is a payload";
  uint8_t *buf;
  msg.type = RegisterCnf2Mstr;
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
  uint8_t buf[2];
  in_port_t port1 = 8080, port2;

  printf("\t\tTest register payload packing/unpacking...");
  pack_register_payload(port1, buf);
  unpack_register_payload(&port2, buf);
  assert(port1 == port2);
  printf("✅\n");
}

void test_compare_shards() {
  CanaryShardInfo arr[4] = {{0, 0, 0}, {3, 0, 0}, {1, 0, 0}};
  printf("\t\tTest with 3 initialized elements...");
  qsort(arr, 3, sizeof(CanaryShardInfo), compare_shards);

  assert(arr[0].id == 0);
  assert(arr[1].id == 1);
  assert(arr[2].id == 3);
  printf("✅\n");

  printf("\t\tTest add element...");
  arr[3] = (CanaryShardInfo){2, 0, 0};
  qsort(arr, 4, sizeof(CanaryShardInfo), compare_shards);
  assert(arr[0].id == 0);
  assert(arr[1].id == 1);
  assert(arr[2].id == 2);
  assert(arr[3].id == 3);
  printf("✅\n");
}
