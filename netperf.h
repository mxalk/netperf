#include <stdint.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#ifndef NETPERF_H
#define NETPERF_H

void server();
void *handle_inc(void *);
void *stream_receiver(void *);

void client(uint16_t, uint64_t, uint16_t, uint64_t, uint8_t, uint16_t);
void *stream_sender(void *);
void print_human_format(unsigned long);

void error(char *, int);
long timedifference_usec(struct timeval, struct timeval);
void hexdump(void *buff, size_t len);

#define DEFAULT_PORT 5201
#define DEFAULT_STREAMS 16
#define DEFAULT_UDP_PACKET_SIZE 512
#define DEFAULT_TIME 10
#define DEFAULT_BANDWIDTH 1 << 20
#define DEFAULT_INTERVAL 1
#define DEFAULT_WAIT 0

#define MAGIC_16 (uint16_t) 0x1337

int tcp_sockfd;
struct sockaddr_in tcp_server_addr;

typedef struct inc_connection Inc_Connection;

struct inc_connection {
    int sockfd;
    struct sockaddr_in address;
    socklen_t len;
    pthread_t *self;
};

#endif // NETPERF_H
