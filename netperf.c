#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

/*
 * RETURN CODES:
 * 0 : OK
 * 1 : Parameter error
 *
 */

#define DEFAULT_ADDRESS "127.0.0.1"
#define DEFAULT_PORT 5901

#define DEFAULT_STREAMS 1
#define DEFAULT_UDP_PACKET_SIZE 128
#define DEFAULT_TIME 10
#define DEFAULT_BANDWIDTH 1 << 20

char *address = DEFAULT_ADDRESS;
uint8_t streams = DEFAULT_STREAMS, mode = 0, delay_mode = 0;
uint16_t  udp_packet_size = DEFAULT_UDP_PACKET_SIZE, port = DEFAULT_PORT;
uint64_t time = DEFAULT_TIME, bandwidth = DEFAULT_BANDWIDTH;

void error(char *, int);
void server();
void client();

int main(int argc, char *argv[]) {
    char *ptr, buffer[256];
    int opt;
    uint64_t temp;

    while((opt = getopt(argc, argv, ":a:p:scl:b:n:t:d")) != -1) {
        switch(opt) {
            // SERVER - CLIENT
            case 'a': // INTERFACE ADDRESS TO BIND TO
                address = strdup(optarg);
                break;
            case 'p': // server: port to listen on, client: port to connect to
                temp = strtoul(optarg, &ptr, 10);
                if (strlen(ptr)) {
                    sprintf(buffer, "Invalid port: %s\n", optarg);
                    error(buffer, 1);
                }
                if (!temp || temp > 65535) {
                    sprintf(buffer, "Port out of range 1-65535(%u)\n", port);
                    error(buffer, 1);
                }
                port = temp;
                break;

            // SERVER
            case 's': // MODE
                if (mode) error("Cannot set mode multiple times\n", 1);
                mode = 1;
                break;

            // CLIENT
            case 'c': // MODE
                if (mode) error("Cannot set mode multiple times\n", 1);
                mode = 2;
                break;
            case 'l': // UDP PACKET SIZE
                temp = strtoul(optarg, &ptr, 10);
                if (strlen(ptr)) {
                    if (strlen(ptr)==1) {
                        switch(*ptr) {
                            case 'K':
                                temp = temp << 10;
                                break;
                            case 'M':
                                temp = temp << 20;
                                break;
                            default:
                                sprintf(buffer, "Invalid UDP packet size: %s\n", optarg);
                                error(buffer, 1);
                        }
                    } else {
                        sprintf(buffer, "Invalid UDP packet size: %s\n", optarg);
                        error(buffer, 1);
                    }
                }
                if (!temp || temp > 65535) {
                    sprintf(buffer, "UDP packet size out of range 1-65535(%lu)\n", temp);
                    error(buffer, 1);
                }
                udp_packet_size = temp;
                break;
            case 'b': // BANDWIDTH
                temp = strtoul(optarg, &ptr, 10);
                if (strlen(ptr)) {
                    if (strlen(ptr)==1) {
                        switch(*ptr) {
                            case 'K':
                                temp = temp << 10;
                                break;
                            case 'M':
                                temp = temp << 20;
                                break;
                            case 'G':
                                temp = temp << 30;
                                break;
                            case 'T':
                                temp = temp << 40;
                                break;
                            default:
                                sprintf(buffer, "Invalid bandwidth: %s\n", optarg);
                                error(buffer, 1);
                        }
                    } else {
                            sprintf(buffer, "Invalid bandwidth: %s\n", optarg);
                            error(buffer, 1);
                    }
                }
                bandwidth = temp;
                break;
            case 'n': // NUMBER OF STREAMS
                temp = strtoul(optarg, &ptr, 10);
                if (strlen(ptr)) {
                    sprintf(buffer, "Invalid number of streams: %s\n", optarg);
                    error(buffer, 1);
                }
                if (!temp || temp > 255) {
                    sprintf(buffer, "Stream number out of range 1-255(%lu)\n", temp);
                    error(buffer, 1);
                }
                streams = temp;
                break;
            case 't': // TIMEOUT - 0=indefinitely
                temp = strtoul(optarg, &ptr, 10);
                if (strlen(ptr)) {
                    sprintf(buffer, "Invalid number of seconds: %s\n", optarg);
                    error(buffer, 1);
                }
                time = temp;
                break;
            case 'd': // ONE WAY DELAY MODE
                delay_mode = 1;
                break;

            // MISC
            case ':':
                error("Option needs a value\n", 1);
            case '?':
                sprintf(buffer, "Unknown option: %c\n", optopt);
                error(buffer, 1);
            default:
                assert(0);
        }
    }

    // optind is for the extra arguments
    // which are not parsed
    for(; optind < argc; optind++){
        printf("extra arguments: %s\n", argv[optind]);
        error("Extra arguments\n", 1);
    }

    printf("Mode: %u\n", mode);
    printf("Address: %s\n", address);
    printf("Port: %u\n", port);
    printf("UDP Packet size: %u\n", udp_packet_size);
    printf("Bandwidth: %lu\n", bandwidth);
    printf("Parralel streams(threads): %u\n", streams);
    printf("Timeout: %lu\n", time);

    switch(mode) {
        case 1:
            server();
            break;
        case 2:
            client();
            break;
        default:
            error("Mode not set (server/client)\n", 1);
    }
    return 0;
}

void error(char *message, int status_code) {
    perror(message);
    exit(status_code);
}

void server() {

}

void client() {

}