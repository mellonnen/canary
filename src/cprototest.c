#include "cproto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_msg_serialization() {
  CanaryMsg msg, msg2;
  char *payload = "this is a payload";
  uint8_t *buf;
  msg.type = RegisterCnf2Mstr;
  msg.payload = (uint8_t *)payload;
  msg.payload_len = strlen(payload) + 1;

  printf("\t\ttest serialized and deserialized structs match...");
  serialize(msg, &buf);

  deserialize(buf, &msg2);
  assert(msg.type == msg2.type);
  assert(msg.payload_len == msg2.payload_len);
  assert(memcmp(msg.payload, msg2.payload, msg.payload_len) == 0);

  printf("✅\n");
}

int main(int argc, char *argv[]) {
  printf("\nTESTS FOR CANARY PROTOCOL HELPERS\n\n");
  printf("\tTesting serialization/deserialization of Canary Messages\n");
  test_msg_serialization();
  return 0;
}
