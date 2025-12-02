CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lrt

all: client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

clean:
	rm -f client server
