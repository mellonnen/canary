CC=gcc
CFLAGS=-Wall -g

all: cprototest

cproto.o: cproto.c cproto.h
	$(CC) $(CFLAGS) -c cproto.c

cprototest: cprototest.c cproto.o
	$(CC) $(CFLAGS) -o $@ $^

test: cprototest
	./cprototest

debug: cprototest
	gdb cprototest

clean:
	rm *.o cprototest
