CC=gcc
CFLAGS=-g -Wall

SRCDIR=src
OBJDIR=obj
TESTDIR=test
BINDIR=bin

CLIENT=$(BINDIR)/client
SHARD=$(BINDIR)/shard
CNF=$(BINDIR)/cnf

LRUTEST=$(TESTDIR)/lrutest
CPROTOTEST=$(TESTDIR)/cprototest

SRCS=$(wildcard $(SRCDIR)/*.c)
BINS=$(CLIENT) $(SHARD) $(CNF)
TESTS=$(LRUTEST) $(CPROTOTEST)


all:$(BINS)

release: CFLAGS=-Wall -O2 -DNDEBUG
release: clean
release: $(BINS)

$(CLIENT): $(OBJDIR)/client.o
$(CNF): $(OBJDIR)/cnf.o
$(SHARD): $(OBJDIR)/cnf.o $(OBJDIR)/lru.o

$(BINS): $(OBJDIR)/cproto.o
	$(CC) $(CFLAGS) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(LRUTEST): $(OBJDIR)/lrutest.o $(OBJDIR)/lru.o
$(CPROTOTEST): $(OBJDIR)/cprototest.o $(OBJDIR)/cproto.o

$(LRUTEST) $(CPROTOTEST):
	$(CC) $(CFLAGS) $^ -o $@

test: $(TESTS)
	run-parts $(TESTDIR)

clean:
	$(RM) -r $(BINDIR)/*  $(OBJDIR)/* $(TESTDIR)/*
