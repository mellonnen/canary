CC=gcc
CFLAGS=-Wall -g

all: lrutest

lru.o: lru.c lru.h
	$(CC) $(CFLAGS) -c lru.c

lrutest: lrutest.c lru.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm *.o *.so lrutest
