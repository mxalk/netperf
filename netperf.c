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
#include <glob.h>

#include "structs.h"

/*
 * RETURN CODES:
 * 0 : OK
 * 1 : Parameter error
 *
 */

#define DEFAULT_PORT 5001
#define DEFAULT_STREAMS 1
#define DEFAULT_UDP_PACKET_SIZE 128
#define DEFAULT_TIME 10
#define DEFAULT_BANDWIDTH 1 << 20

#define MAGIC_16 (uint16_t) 0x1337

__thread char buffer[256];
__thread uint8_t *net_buffer;
__thread size_t net_bufer_size;
char *address = NULL;
uint8_t streams = DEFAULT_STREAMS, mode = 0, delay_mode = 0;
uint16_t port = DEFAULT_PORT;
__thread uint16_t udp_packet_size = DEFAULT_UDP_PACKET_SIZE;
uint64_t c_time = DEFAULT_TIME, bandwidth = DEFAULT_BANDWIDTH;

void error(char *, int);
void setup();
void server();
void client();
void *handle_inc(void *);
void hexdump(void *buff, size_t len);

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

    printf("            _                    __ \n"
            "           | |                  / _|\n"
            " _ __   ___| |_ _ __   ___ _ __| |_ \n"
            "| '_ \\ / _ \\ __| '_ \\ / _ \\ '__|  _|\n"
            "| | | |  __/ |_| |_) |  __/ |  | |  \n"
            "|_| |_|\\___|\\__| .__/ \\___|_|  |_|  \n"
            "               | |                  \n"
            "               |_|                  \n"
            "\n");
    printf("Mode: %u\n", mode);
    printf("Address: %s\n", address);
    printf("Port: %u\n", port);
    printf("UDP Packet size: %u\n", udp_packet_size);
    printf("Bandwidth: %lu\n", bandwidth);
    printf("Parralel streams(threads): %u\n", streams);
    printf("Timeout: %lu\n", c_time);
    printf("----------------------------------------\n");

    setup();
    return 0;
}

void error(char *message, int status_code) {
    perror(message);
    exit(status_code);
}

int tcp_sockfd;
struct sockaddr_in tcp_server_addr;

void setup() {

    tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sockfd == -1) error("Socket creation failed...\n", 2);
    memset(&tcp_server_addr, 0, sizeof(struct sockaddr_in));
    tcp_server_addr.sin_family = AF_INET;
    if (address) {
        if (!inet_pton(AF_INET, address, &tcp_server_addr.sin_addr)) {
            sprintf(buffer, "Server address invalid: %s\n", address);
            error(buffer, 2);
        }
        free(address);
    } else if (mode == 2) error("Cannot choose client mode without server address.\n", 1);
    tcp_server_addr.sin_port = htons(port);

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
}

void server() {
    pthread_t *worker;
    Inc_Connection *remote;
    struct sockaddr_in clientname;
    socklen_t len;
    int sockfd;

    printf("---------------- SERVER ----------------\n");
    if (bind(tcp_sockfd, (struct sockaddr *) &tcp_server_addr, sizeof(tcp_server_addr))) error("Socket bind failed.\n", 2);
    if (listen(tcp_sockfd, 5)) error("Socket listen failed.\n", 2);
    while (1) {
        sockfd = accept(tcp_sockfd, (struct sockaddr *) &clientname, &len);
        if (sockfd < 0) continue;
        printf("Incoming connection from '%s' port '%u'\n", inet_ntoa(clientname.sin_addr), ntohs(clientname.sin_port));
        remote = malloc(sizeof(Inc_Connection));
        remote->sockfd = sockfd;
        remote->address = clientname;
        remote->len = len;
        worker = malloc(sizeof(pthread_t));
        remote->self = worker;
        if (pthread_create(worker, NULL, handle_inc, remote)) {
            fprintf(stderr, "Error spawning worker\n");
            free(worker);
            free(remote);
        }
    }
}

void client() {
    struct sockaddr_in tcp_self_addr, udp_self_addr, udp_serv_addr;
    int udp_sockfd, rnd;
    socklen_t len;
    Header h;
    uint8_t *tmp;
    size_t size;
    uint64_t e_time = 0;

    printf("---------------- CLIENT ----------------\n");
    // BUFFERS
    net_buffer = malloc(sizeof(uint16_t));
    net_bufer_size = sizeof(uint16_t);

    // CREATE TCP SOCKET
    memset(&tcp_self_addr, 0, sizeof(struct sockaddr_in));
    tcp_self_addr.sin_family = AF_INET;
    tcp_self_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_self_addr.sin_port = htons(0);
    if (bind(tcp_sockfd, (struct sockaddr *) &tcp_self_addr, sizeof(tcp_self_addr))) error("Socket bind failed.\n", 2);
    if (connect(tcp_sockfd, (struct sockaddr *) &tcp_server_addr, sizeof(tcp_server_addr))) error("Connection to server failed.\n", 2);

    // send udp_packet_size to server through tcp
    *(uint16_t *)net_buffer = htons(udp_packet_size);
    send(tcp_sockfd, net_buffer, net_bufer_size, 0);

    // SETUP UDP SERVER ADDRESS
    net_bufer_size = sizeof(struct sockaddr_in);
    net_buffer = realloc(net_buffer, net_bufer_size);
    recv(tcp_sockfd, net_buffer, net_bufer_size, 0);
    udp_serv_addr = tcp_server_addr;
    udp_serv_addr.sin_port = *(uint16_t *)net_buffer;
    printf("Server UDP port: '%u'\n", ntohs(udp_serv_addr.sin_port));

    // SETUP UDP SELF SOCKET
    if ((udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) error("Socket creation failed", 2);
    memset(&udp_self_addr, 0, sizeof(struct sockaddr_in));
    udp_self_addr.sin_family = AF_INET;
    udp_self_addr.sin_addr.s_addr = INADDR_ANY;
    udp_self_addr.sin_port = htons(0);
    if (bind(udp_sockfd, (struct sockaddr *) &udp_self_addr, sizeof(udp_self_addr))) error("Socket bind failed.\n", 2);

    len = sizeof(udp_self_addr);
    if (getsockname(udp_sockfd, (struct sockaddr *)&udp_self_addr, &len) == -1) error("Getsockname error\n", 2);
    printf("UDP open, port '%u'\n", ntohs(udp_self_addr.sin_port));


    bzero(&h, sizeof(Header));
    memcpy( &(h.id), "netperf", strlen("netperf")+1);
    h.type = htons(0x01);
    h.length = htons(udp_packet_size);
    h.header_fin = htons(MAGIC_16);

    net_bufer_size = sizeof(uint8_t)*udp_packet_size;
    net_buffer = realloc(net_buffer, net_bufer_size);
    tmp = net_buffer;
    size = sizeof(Header);
    memcpy(tmp, &h, size);
    tmp += size;

    size = sizeof(uint8_t)*udp_packet_size-sizeof(Header)-sizeof(MAGIC_16);
    rnd = open("/dev/urandom", O_RDONLY);
    read(rnd, tmp, size);
    close(rnd);
    tmp += size;
    *(uint16_t *)tmp = htons(MAGIC_16);
    do {
        printf("Sending Header & payload size %u\n", net_bufer_size);
        sendto(udp_sockfd, net_buffer, net_bufer_size, 0, (struct sockaddr *) &udp_serv_addr, sizeof(udp_serv_addr));
        sleep(1);
    } while (++e_time != c_time);

}


void *handle_inc(void *data) {
    struct sockaddr_in udp_self_addr, from_addr;
    socklen_t len;
    size_t size;
    Header *h;
    Inc_Connection *conn = (Inc_Connection *)data;
    int udp_sockfd;

    printf("Worker handling connection from '%s' port '%u'\n", inet_ntoa(conn->address.sin_addr), ntohs(conn->address.sin_port));

    // BUFFERS
    net_bufer_size = sizeof(uint16_t);
    net_buffer = malloc(net_bufer_size);

    // reveive udp_packet_size through tcp
    size = recv(conn->sockfd, net_buffer, sizeof(uint16_t), 0);
    udp_packet_size = ntohs(*(uint16_t *)net_buffer);
    printf("Client UDP packet size is %u\n", udp_packet_size);

    // SETUP UDP SELF
    net_bufer_size = sizeof(Header)+sizeof(uint8_t)*udp_packet_size;
    net_buffer = realloc(net_buffer, net_bufer_size);
    if ((udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) error("Socket creation failed", 2);
    memset(&udp_self_addr, 0, sizeof(struct sockaddr_in));
    udp_self_addr.sin_family = AF_INET;
    udp_self_addr.sin_addr.s_addr = tcp_server_addr.sin_addr.s_addr;
    udp_self_addr.sin_port = htons(0);
    if (bind(udp_sockfd, (struct sockaddr *) &udp_self_addr, sizeof(udp_self_addr))) error("Socket bind failed.\n", 2);
    printf("Binding UDP address to '%s'\n", inet_ntoa(udp_self_addr.sin_addr));

    // send UDP port
    len = sizeof(udp_self_addr);
    if (getsockname(udp_sockfd, (struct sockaddr *)&udp_self_addr, &len) == -1) error("Getsockname\n", 2);
    printf("UDP open, port '%u'\n", ntohs(udp_self_addr.sin_port));
    send(conn->sockfd, &udp_self_addr.sin_port, sizeof(uint16_t), 0);

    // RECEIVE DATA
    net_bufer_size = sizeof(uint8_t)*udp_packet_size;
    net_buffer = realloc(net_buffer, net_bufer_size);
    do {
        size = recvfrom(udp_sockfd, net_buffer, net_bufer_size, 0, (struct sockaddr *) &from_addr, &len);
        printf("UDP data from '%s' port '%u' size '%zu'\n", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), size);
    } while (size);

    close(udp_sockfd);
    close(conn->sockfd);
    free(conn->self);
    free(data);
    printf("Worker destruction\n");
}