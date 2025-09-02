# Makefile for CS:APP Proxy Lab (Linux)
# usage:
#   make            # build proxy and tiny
#   make clean
#   make run-proxy PORT=15214
#   make run-tiny  TINY_PORT=15213
#   make handin    # packs files for submission

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -g -std=c11 \
           -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS := -lpthread

BINARIES := proxy tiny
OBJS     := csapp.o

all: $(BINARIES)

# ----- proxy -----
proxy: proxy.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

proxy.o: proxy.c csapp.h
	$(CC) $(CFLAGS) -c $< -o $@

# ----- tiny -----
tiny: tiny.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tiny.o: tiny.c csapp.h
	$(CC) $(CFLAGS) -c $< -o $@

# ----- common -----
csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c -o csapp.o

clean:
	rm -f *.o $(BINARIES) proxylab-handin.tar

# Create a handin tarball (adjust the file list as needed)
handin:
	tar -cvf proxylab-handin.tar \
		proxy.c csapp.c csapp.h Makefile \
		home.html tiny.c

# Convenience run targets
run-proxy: proxy
	@test -n "$(PORT)" || (echo "Usage: make run-proxy PORT=15214"; exit 1)
	./proxy $(PORT)

run-tiny: tiny
	@test -n "$(TINY_PORT)" || (echo "Usage: make run-tiny TINY_PORT=15213"; exit 1)
	./tiny $(TINY_PORT)

.PHONY: all clean handin run-proxy run-tiny
