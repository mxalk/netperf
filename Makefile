CC=gcc
CCFLAGS= -o bin/$@ -g

SERVER_FLAGS= -s $(BOTH)
CLIENT_FLAGS= -c  $(BOTH)

BOTH=-p 5001

netperf: netperf.c
	$(CC) netperf.c -pthread $(CCFLAGS)

server: netperf
	./netperf -s

client: netperf
	./netperf -c -l 1K -b 100M -n 4 -t 10

test: server client
