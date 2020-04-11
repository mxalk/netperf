//
// Created by manos on 3/27/20.
//

#include "netperf.h"


void client() {
    struct sockaddr_in tcp_self_addr, udp_self_addr, udp_serv_addr;
    int udp_sockfd, rnd_fd;
    socklen_t len;
    Header h;
    uint8_t *tmp;
    size_t size;
    uint64_t e_time = 0;

    size_t net_bufer_size;
    uint8_t *net_buffer, streams = DEFAULT_STREAMS, mode = 0, delay_mode = 0;
    uint16_t udp_packet_size = DEFAULT_UDP_PACKET_SIZE, port = DEFAULT_PORT;
    uint64_t c_time = DEFAULT_TIME, bandwidth = DEFAULT_BANDWIDTH;

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


    // bandwidth = threads * packets_per_sec * packet_size
    // packets_per_sec = bandwidth/(threads*packet_size)
    // interval = 1/packets_per_sec

    // time1
    // send
    // time2
    // delta = time2-time1
    // sleep (interval-delta)
    // busy time = delta/interval

    unsigned int packets_per_sec = bandwidth/(streams*udp_packet_size);
    float interval = 1/packets_per_sec, sleep_time;
//    float interval = ((float)(streams*udp_packet_size*8))/bandwidth;
    unsigned int i;
    pthread_t worker, *workers;


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
    rnd_fd = open("/dev/urandom", O_RDONLY);
    read(rnd_fd, tmp, size);
    close(rnd_fd);
    tmp += size;
    *(uint16_t *)tmp = htons(MAGIC_16);

    workers = malloc(sizeof(pthread_t)*streams);
    for (i=0; i<streams; i++) {
        if (pthread_create(&worker, NULL, handle_inc, stream)) {
            fprintf(stderr, "Error spawning worker\n");
        }
        workers[i] = worker;
    }
    do {
//        printf("Second\n");
        sleep(sleep_time);
    } while (++e_time != c_time);
    for (i=0; i<streams; i++) {
        pthread_join(workers[i], NULL);
    }
    free(workers);
}

void *stream(void *data) {
    struct timeval init, start, end;
    short stop = 0;

//    gettimeofday(&init, NULL);
//    printf("%ld.%06ld\n", init.tv_sec, init.tv_usec);
    while (1) {
        if (stop) break;
        for (i=0; i<packets_per_sec; i++) {
            gettimeofday(&start, NULL);
            sendto(udp_sockfd, net_buffer, net_bufer_size, 0, (struct sockaddr *) &udp_serv_addr,
                   sizeof(udp_serv_addr));
            gettimeofday(&end, NULL);
            sleep_time = interval - timedifference_msec(start, end);
            if (sleep_time < 0) sleep_time = 0;
            sleep(sleep_time);
        }
    }
    return NULL;
}
