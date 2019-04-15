.PHONY: all debug release clean

CC=gcc

COMMON_CFLAGS=-march=native
DEBUG_FLAGS=-g -O0 -DDEBUG -fno-omit-frame-pointer
RELEASE_FLAGS=-O3 -DNDEBUG
INCLUDE_FLAGS=-I.
WARN_FLAGS=-Wall -Wextra -Wshadow

# gcc on students has completly broken sanitizer dependencies.
SANITIZERS= #-fsanitize=address,undefined

COMMON_OBJ=common.o exbuffer.o
CLIENT_OBJ=klient.o
SERVER_OBJ=serwer.o

CLIENT_EXE=netstore-client
SERVER_EXE=netstore-server

release: CFLAGS=$(COMMON_CFLAGS) $(RELEASE_FLAGS) $(INCLUDE_FLAGS)
release: all

debug: CFLAGS=$(COMMON_CFLAGS) $(SANITIZERS) $(DEBUG_FLAGS) $(INCLUDE_FLAGS)
debug: all

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

all: $(CLIENT_EXE) $(SERVER_EXE)


$(CLIENT_EXE): $(COMMON_OBJ) $(CLIENT_OBJ)
	$(CC) $(COMMON_OBJ) $(CLIENT_OBJ) -o $(CLIENT_EXE)

$(SERVER_EXE): $(COMMON_OBJ) $(SERVER_OBJ)
	$(CC) $(COMMON_OBJ) $(SERVER_OBJ) -o $(SERVER_EXE)

clean:
	@rm -f *.o
	@rm -f netstore-server
	@rm -f netstore-client
