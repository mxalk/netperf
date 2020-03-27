#include "netperf.h"

/*
 * RETURN CODES:
 * 0 : OK
 * 1 : Parameter error
 *
 */

char buffer[256];

char *address = NULL;
uint8_t streams = DEFAULT_STREAMS, mode = 0, delay_mode = 0;
uint16_t port = DEFAULT_PORT, udp_packet_size = DEFAULT_UDP_PACKET_SIZE;
uint64_t c_time = DEFAULT_TIME, bandwidth = DEFAULT_BANDWIDTH;

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

float timedifference_msec(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000.0f + (end.tv_usec - start.tv_usec) / 1000.0f;
}

void hexdump(void *buff, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", ((char *) buff)[i]);
        if (i%8 == 7) printf("\n");
    }
    printf("\n");
}