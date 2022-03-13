#include "../lib/client/client.h"
#include <bits/getopt_core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CNF_PORT 8080
#define DEFAULT_CNF_ADDR "127.0.0.1"

CanaryCache cache;

void run_shell();
char *read_line();
void execute_cmd(char *);

int main(int argc, char *argv[]) {
  int opt;
  char *cnf_addr = malloc(20);
  in_port_t cnf_port = DEFAULT_CNF_PORT;
  strcpy(cnf_addr, DEFAULT_CNF_ADDR);

  while ((opt = getopt(argc, argv, "p:a:")) != -1) {
    switch (opt) {
    case 'p':
      cnf_port = atoi(optarg);
      break;
    case 'a':
      memset(cnf_addr, 0, strlen(cnf_addr) + 1);
      strcpy(cnf_addr, optarg);
      break;
    default:
      printf("Usage: %s [-a <cnf-address>] [-p <cnf-port>]\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  cache.cnf_addr = malloc(strlen(cnf_addr) + 1);
  cache = create_canary_cache(cnf_addr, cnf_port);
  printf("Welcome to the Canary-cli!\n\n This is an interface for the Canary "
         "distributed cache,\n make sure that you have started the "
         "configuration service and data shards!\n\n");
  run_shell();
}

void run_shell() {
  char *line;
  while (1) {
    printf("( 🐦 ) ▶ ");
    line = read_line();
    execute_cmd(line);
    free(line);
  }
}

char *read_line() {
  char *line = NULL;
  size_t bufsize = 0; // have getline allocate a buffer for us

  if (getline(&line, &bufsize, stdin) == -1) {
    if (feof(stdin)) {
      exit(EXIT_SUCCESS); // We recieved an EOF
    } else {
      perror("readline");
      exit(EXIT_FAILURE);
    }
  }
  line[strcspn(line, "\n")] = 0;
  return line;
}

void execute_cmd(char *line) {
  char *cmd = strtok(line, " ");
  char *key = strtok(NULL, " ");

  if (strcmp(cmd, "get") == 0) {
    int *value = canary_get(&cache, key);
    if (value == NULL) {
      printf("No cached value found!\n");
    } else {
      printf("Got value %d !\n", *value);
    }
  } else if (strcmp(cmd, "put") == 0) {
    int value = atoi(strtok(NULL, " "));
    canary_put(&cache, key, value);
    printf("Cached key value pair (%s, %d)!\n", key, value);
  } else {
    printf("\"%s\" is not a valid command ! try \"put\" or \"get\"!\n", cmd);
  }
  printf("\n");
}
