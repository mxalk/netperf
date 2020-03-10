#include <stdint.h>

#ifndef NETPERF_STRUCTS_H
#define NETPERF_STRUCTS_H

typedef struct header Header;

struct header {
    char id;
    uint8_t type;
    uint32_t length;
};


#endif //NETPERF_STRUCTS_H
