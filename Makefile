# ------------- VARIABLES ----------------

# Complier stuff
CC=gcc
CFLAGS=-g -Wall
LDFLAGS=-lpthread

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
$(BINS):$(BINDIR)/%: $(SRCDIR)/%.c $(OBJS) $(SRCDIR)/constants.h
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

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


run: $(BINDIR) $(OBJDIR) $(BINS)
	tmux new-session -d -s canary
	tmux new-window -n main 
	tmux split-window -h "./bin/cnf"
	tmux new-window -n shard   
	tmux split-window -h "./bin/shard -p 6000"
	tmux split-window -v  "./bin/shard -p 6001"
	tmux select-pane -t 0
	tmux split-window -v "./bin/shard -p 6002 -f"
	tmux send-keys -t 0 "./bin/shard -p 6003 -f" C-m
	tmux select-window -t main
	tmux select-pane -t 0
	tmux send-keys "./bin/canary-cli" C-m
	tmux attach-session -d -t canary




# Clean up object files and binaries.
clean:
	$(RM) -r $(BINDIR)/*  $(OBJDIR)/* $(TESTDIR)/bin/*
