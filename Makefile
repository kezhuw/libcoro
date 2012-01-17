CC	:= gcc
CFLAGS	:= -Wall
LDFLAGS	:= -levent

all: corotest

corotest: coro.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)