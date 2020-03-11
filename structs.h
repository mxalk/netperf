#include <stdint.h>
#include <arpa/inet.h>

#ifndef NETPERF_STRUCTS_H
#define NETPERF_STRUCTS_H

typedef struct header Header;

struct header {
    char id;
    uint8_t type;
    uint32_t length;
};

typedef struct inc_connection Inc_Connection;

struct inc_connection {
    int sockfd;
    struct sockaddr_in address;
    unsigned int len;
};


#endif //NETPERF_STRUCTS_H
