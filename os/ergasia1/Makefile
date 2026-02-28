CC = gcc
CFLAGS = -Wall -g

all: chat

chat: chat.c chat.h
	$(CC) $(CFLAGS) -o chat chat.c

clean:
	rm -f chat
	ipcrm -a 2>/dev/null || true

run: chat
	./chat