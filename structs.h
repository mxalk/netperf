#include <stdint.h>
#include <arpa/inet.h>

#ifndef NETPERF_STRUCTS_H
#define NETPERF_STRUCTS_H

typedef struct header Header;

struct header {
    char id[8];
    uint8_t type;
    uint32_t length;
};

typedef struct inc_connection Inc_Connection;

struct inc_connection {
    int sockfd;
    struct sockaddr_in address;
    socklen_t len;
    pthread_t *self;
};

#endif //NETPERF_STRUCTS_H
