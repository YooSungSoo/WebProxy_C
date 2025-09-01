# ================== Toolchain ==================
CC      = gcc
CSTD    = -std=c11
CWARN   = -Wall -Wextra
COPT    = -O2
CDEFS   = -D_POSIX_C_SOURCE=200112L
CFLAGS  = $(CSTD) $(CWARN) $(COPT) $(CDEFS)

LDFLAGS =
LDLIBS  =

# ----- Optional toggles -----
# make DEBUG=1         -> add -g
# make SAN=1           -> add ASan/UBSan
ifeq ($(DEBUG),1)
  CFLAGS += -g
endif
ifeq ($(SAN),1)
  CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS += -fsanitize=address,undefined
endif

# ================== Sources / Objects ==================
HDRS       = csapp.h
COMMON_OBJ = csapp.o
CLIENT_OBJ = echoclient.o
SERVER_OBJ = echoserver.o

TARGETS = echoclient echoserver

# ================== Default ==================
all: $(TARGETS)

# ================== Link rules ==================
echoclient: $(CLIENT_OBJ) $(COMMON_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

echoserver: $(SERVER_OBJ) $(COMMON_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# ================== Compile rules ==================
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# ================== Convenience ==================
clean:
	rm -f $(TARGETS) *.o

rebuild: clean all

# Run helpers:
#   make run-server PORT=8080
#   make run-client HOST=localhost PORT=8080
PORT ?= 8080
HOST ?= localhost

run-server: echoserver
	./echoserver $(PORT)

run-client: echoclient
	./echoclient $(HOST) $(PORT)

.PHONY: all clean rebuild run-server run-client
