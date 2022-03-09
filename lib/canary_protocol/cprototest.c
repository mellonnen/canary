#include "cproto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_serialization() {
  CanaryMsg msg;
  char *payload = "this is a payload";
  uint8_t *buf;
  msg.type = register_cnf2mstr;
  msg.payload = (uint8_t *)payload;
  msg.payload_len = strlen(payload) * sizeof(uint8_t);

  printf("\ttest serialization and deserialization...");
  buf = serialize(msg);

  CanaryMsg *msg2 = deserialize(buf);
  assert(msg.type == msg2->type);
  assert(msg.payload_len == msg2->payload_len);
  assert(memcmp(msg.payload, msg2->payload, msg.payload_len) == 0);

  printf("✅\n");
}

int main(int argc, char *argv[]) {
  test_serialization();
  return 0;
}
