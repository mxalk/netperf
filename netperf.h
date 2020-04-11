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

#ifndef NETPERF_H
#define NETPERF_H

void error(char *, int);
void server();
void *handle_inc(void *);
void client(uint16_t, uint64_t, uint8_t, uint64_t, uint8_t, uint16_t);
void *stream(void *);
float timedifference_msec(struct timeval, struct timeval);
void hexdump(void *buff, size_t len);

#define DEFAULT_PORT 5001
#define DEFAULT_STREAMS 4
#define DEFAULT_UDP_PACKET_SIZE 128
#define DEFAULT_TIME 10
#define DEFAULT_BANDWIDTH 1 << 20
#define DEFAULT_INTERVAL 1
#define DEFAULT_WAIT 0

#define MAGIC_16 (uint16_t) 0x1337

int tcp_sockfd;
struct sockaddr_in tcp_server_addr;

typedef struct header Header;

struct header {
    char id[8];
    uint16_t type;
    uint16_t length;
    uint16_t header_fin;
};

typedef struct inc_connection Inc_Connection;

struct inc_connection {
    int sockfd;
    struct sockaddr_in address;
    socklen_t len;
    pthread_t *self;
};

//typedef struct client_conf Client_Conf;
//
//struct client_conf {
//
//    uint8_t delay_mode;
//    uint64_t c_time, bandwidth;
//};

double throughput(double packets, double avePacketSize, double time);
double goodPut(double packets, double avePacketSize, double time); 

#endif // NETPERF_H
