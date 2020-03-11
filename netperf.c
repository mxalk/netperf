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

#include "structs.h"

/*
 * RETURN CODES:
 * 0 : OK
 * 1 : Parameter error
 *
 */

#define DEFAULT_PORT 5901
#define DEFAULT_STREAMS 1
#define DEFAULT_UDP_PACKET_SIZE 128
#define DEFAULT_TIME 10
#define DEFAULT_BANDWIDTH 1 << 20

__thread char buffer[256];
char *address = NULL;
uint8_t streams = DEFAULT_STREAMS, mode = 0, delay_mode = 0;
uint16_t  udp_packet_size = DEFAULT_UDP_PACKET_SIZE, port = DEFAULT_PORT;
uint64_t c_time = DEFAULT_TIME, bandwidth = DEFAULT_BANDWIDTH;

void error(char *, int);
void setup();
void server(int sockfd, struct sockaddr_in server_addr);
void client(int sockfd, struct sockaddr_in server_addr);
void *handle_inc(void *);

int main(int argc, char *argv[]) {
    char *ptr;
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
                c_time = temp;
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

//    printf("Mode: %u\n", mode);
//    printf("Address: %s\n", address);
//    printf("Port: %u\n", port);
//    printf("UDP Packet size: %u\n", udp_packet_size);
//    printf("Bandwidth: %lu\n", bandwidth);
//    printf("Parralel streams(threads): %u\n", streams);
//    printf("Timeout: %lu\n", c_time);

    setup();
    return 0;
}

void error(char *message, int status_code) {
    perror(message);
    exit(status_code);
}

void setup() {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) error("Socket creation failed...\n", 2);
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    if (address) {
        if (!inet_pton(AF_INET, address, &server_addr.sin_addr)) {
            sprintf(buffer, "Server address invalid: %s\n", address);
            error(buffer, 2);
        }
        free(address);
    } else error("Cannot choose client mode without server address.\n", 1);
    server_addr.sin_port = htons(port);

    switch(mode) {
        case 1:
            server(sockfd, server_addr);
            break;
        case 2:
            client(sockfd, server_addr);
            break;
        default:
            error("Mode not set (server/client)\n", 1);
    }
}

void server(int sockfd, struct sockaddr_in server_addr) {
    pthread_t worker;
    Inc_Connection *client;

    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr))) error("Socket bind failed.\n", 2);
    if (listen(sockfd, 5)) error("Socket listen failed.\n", 2);
    while (1) {
        client = malloc(sizeof(Inc_Connection));
        client->sockfd = accept(sockfd, (struct sockaddr *) &(client->address), &(client->len));
        if(pthread_create(&worker, NULL, handle_inc, client)) {
            fprintf(stderr, "Error spawning worker\n");
            free(client);
        }
    }
}

void client(int sockfd, struct sockaddr_in server_addr) {
    struct sockaddr_in self_addr;

    self_addr.sin_family = AF_INET;
    self_addr.sin_addr.s_addr = INADDR_ANY;
    self_addr.sin_port = htons(0);

    if (bind(sockfd, (struct sockaddr *) &self_addr, sizeof(self_addr))) error("Socket bind failed.\n", 2);
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr))) error("Connection to server failed.\n", 2);
}

void *handle_inc(void *data) {
    Inc_Connection conn = *(Inc_Connection *)data;
    free(data);
    printf("Incoming connection from '%s' port '%u'\n", inet_ntoa(conn.address.sin_addr), conn.address.sin_port);
}