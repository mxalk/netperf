CC=gcc
CCFLAGS= -o $@ -g

SERVER_FLAGS= -s $(BOTH)
CLIENT_FLAGS= -c  $(BOTH)

BOTH=-p 5001

netperf: netperf.c netperf.h server.c client.c
	$(CC) netperf.c server.c client.c -pthread $(CCFLAGS)

server: netperf
	./netperf -s

client: netperf
	./netperf -c -l 1K -b 1G -n 4 -t 15

test: server client

clean:
	rm netperf