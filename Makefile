.PHONY: all debug release clean clang_complete

CC=gcc
CFLAGS=-march=native

DISABLED_WARNINGS=-Wno-padded \
                  -Wno-sign-conversion
SANITIZERS=-fsanitize=address,undefined
ifeq ($(CC),clang++)
	WARN_FLAGS=-Weverything
	DISABLED_WARNINGS += -Wno-c++98-compat-pedantic \
			     -Wno-gnu-zero-variadic-macro-arguments \
			     -Wno-vla -Wno-vla-extension
else
	WARN_FLAGS=-Wall -Wextra -Wshadow
endif

DEBUG_FLAGS=-g -O0 -DDEBUG -fno-omit-frame-pointer
RELEASE_FLAGS=-O3 -DNDEBUG
INCLUDE_FLAGS=-I.

all: debug

debug:
	$(CC) -c $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(DEBUG_FLAGS)   \
	$(INCLUDE_FLAGS) $(SANITIZERS) common.c -o common.o

	$(CC) -c $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(DEBUG_FLAGS)   \
	$(INCLUDE_FLAGS) $(SANITIZERS) exbuffer.c -o exbuffer.o

	$(CC) $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(DEBUG_FLAGS)   \
	$(INCLUDE_FLAGS) $(SANITIZERS) serwer.c common.o exbuffer.o -o netstore-server

	$(CC) $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(DEBUG_FLAGS)   \
	$(INCLUDE_FLAGS) $(SANITIZERS) klient.c common.o exbuffer.o -o netstore-client

release:
	$(CC) -c $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(RELEASE_FLAGS)   \
	$(INCLUDE_FLAGS) common.c -o common.o

	$(CC) $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(RELEASE_FLAGS) \
	$(INCLUDE_FLAGS) serwer.c common.o -o netstore-server

	$(CC) $(CFLAGS) $(WARN_FLAGS) $(DISABLED_WARNINGS) $(RELEASE_FLAGS) \
	$(INCLUDE_FLAGS) klient.c common.o -o netstore-client

clang_complete:
	echo $(INCLUDE_FLAGS) $(LINKER_FLAGS) > .clang_complete

clean:
	rm netstore-server
	rm netstore-client
