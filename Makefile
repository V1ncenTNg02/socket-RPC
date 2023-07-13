CC=cc
LDFLAGS = -lm
CFLAGS = -c -Wall

.PHONY: all clean

all: rpc-server rpc-client

rpc-server: rpc.o server.o
	$(CC) $(LDFLAGS) -o $@ $^

rpc-client: rpc.o client.o
	$(CC) $(LDFLAGS) -o $@ $^

rpc.o: rpc.c
	$(CC) $(CFLAGS) -o $@ $<

server.o: server.c
	$(CC) $(CFLAGS) -o $@ $<

client.o: client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f *.o rpc-server rpc-client 

