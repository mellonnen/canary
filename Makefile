# ------------- VARIABLES ----------------

# Complier stuff
CC=gcc
CFLAGS=-g -Wall

# directories
SRCDIR=src
LIBDIR=lib
OBJDIR=obj
BINDIR=bin
TESTDIR=test


# Map source code files to binaries
SRCS=$(wildcard $(SRCDIR)/*.c)
BINS=$(patsubst $(SRCDIR)/%.c, $(BINDIR)/%, $(SRCS))

# Map library source code files to .o files
LIBS=$(wildcard $(LIBDIR)/**/*.c)
OBJS=$(addprefix $(OBJDIR)/,$(notdir $(patsubst $(LIBDIR)/%.c, $(LIBDIR)/%.o, $(LIBS))))

# Map test source code files to test binaries
TESTS=$(wildcard $(TESTDIR)/*.c)
TESTBINS=$(patsubst $(TESTDIR)/%.c, $(TESTDIR)/bin/%, $(TESTS))


# ------------- TARGETS -------------------

all: $(BINDIR) $(OBJDIR) $(BINS)

# Compile binaries.
$(BINS):$(BINDIR)/%: $(SRCDIR)/%.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# Compile library object files.
.SECONDEXPANSION:
$(OBJS):$(OBJDIR)/%.o: $(LIBDIR)/$$*/$$*.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile test binaries.
$(TESTBINS):$(TESTDIR)/bin/%: $(TESTDIR)/%.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@


# Create ignored directories if does not exist.
$(BINDIR) $(OBJDIR) $(TESTDIR)/bin:
	mkdir $@

# ------------- COMMANDS -------------------

# Compile without debug flags and with optimizations.
release: CFLAGS=-Wall -O2 -DNDEBUG
release: clean
release: $(BINS)

# Run tests.
test: $(TESTDIR)/bin $(OBJDIR) $(TESTBINS) 
	run-parts $(TESTDIR)/bin

# Clean up object files and binaries.
clean:
	$(RM) -r $(BINDIR)/*  $(OBJDIR)/* $(TESTDIR)/bin/*
