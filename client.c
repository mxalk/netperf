//
// Created by manos on 3/27/20.
//

#include "netperf.h"

void client(uint16_t udp_packet_size, uint64_t bandwidth, uint8_t streams, uint64_t c_time, uint8_t delay_mode, uint16_t wait)
{
    struct sockaddr_in tcp_self_addr, *udp_self_addr, *udp_serv_addr;
    int *udp_sockfd, rnd_fd, i;
    socklen_t len;
    Header h;
    uint8_t *tmp;
    size_t size;
    uint64_t e_time = 0;

    size_t net_bufer_size;
    uint16_t *net_buffer;

    pthread_t *workers;

    printf("---------------- CLIENT ----------------\n");
    printf("UDP Packet size: %u\n", udp_packet_size);
    printf("Bandwidth: %lu\n", bandwidth);
    printf("Parralel streams(threads): %u\n", streams);
    printf("Experiment Time: %lu\n", c_time);
    printf("One Way delay mode: %u\n", delay_mode);
    printf("Wait time: %u\n", wait);

    // CREATE TCP SOCKET
    memset(&tcp_self_addr, 0, sizeof(struct sockaddr_in));
    tcp_self_addr.sin_family = AF_INET;
    tcp_self_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_self_addr.sin_port = htons(0);
    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_self_addr, sizeof(tcp_self_addr)))
        error("Socket bind failed.\n", 2);
    if (connect(tcp_sockfd, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)))
        error("Connection to server failed.\n", 2);

    // 1. send udp_packet_size, # streams, delay_mode
    // BUFFERS
    net_bufer_size = sizeof(uint16_t) * 3;
    net_buffer = malloc(net_bufer_size);

    // send to server through tcp
    net_buffer[0] = htons(udp_packet_size);
    net_buffer[1] = htons(streams);
    net_buffer[2] = htons(delay_mode);
    send(tcp_sockfd, net_buffer, net_bufer_size, 0);

    // 4. receive #streams udp server addresses.
    net_bufer_size = sizeof(uint16_t) * streams;
    net_buffer = realloc(net_buffer, net_bufer_size);
    udp_serv_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_self_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_sockfd = malloc(sizeof(int) * streams);
    workers = malloc(sizeof(pthread_t) * streams);
    recv(tcp_sockfd, net_buffer, net_bufer_size, 0);
    // SERVER UDP SOCKETS
    for (i = 0; i < streams; i++)
    {
        // setup server socket
        memcpy(&tcp_server_addr, &(udp_serv_addr[i]), sizeof(struct sockaddr_in));
        udp_serv_addr[i].sin_port = net_buffer[i];
        // setup self socket
        memcpy(&tcp_self_addr, &(udp_self_addr[i]), sizeof(struct sockaddr_in));
        udp_self_addr[i].sin_port = htons(0);
        if ((udp_sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            error("Socket creation failed", 2);
        if (bind(udp_sockfd[i], (struct sockaddr *)&(udp_self_addr[i]), sizeof(struct sockaddr)))
            error("Socket bind failed.\n", 2);
        if (getsockname(udp_sockfd[i], (struct sockaddr *)&(udp_self_addr[i]), &len) == -1)
            error("Getsockname\n", 2);
    }
    printf("Remote UDP: %u", ntohs(udp_serv_addr[0].sin_port));
    for (i = 1; i < streams; i++)
        printf(", %u", ntohs(udp_serv_addr[i].sin_port));
    printf(".\n");
    printf("Self UDP: %u", ntohs(udp_self_addr[0].sin_port)); // ERROR - FIRST PORT IS 0
    for (i = 1; i < streams; i++)
        printf(", %u", ntohs(udp_self_addr[i].sin_port));
    printf(".\n");

    if (wait) {
        printf("Introducing the delay of %u seconds before the measurement begins.\n");
        sleep(wait);
    }
    for (i = 0; i < streams; i++)
    {
        // here should be broken to one udp_serv_addr per thread
        // WORKER

        // TODO: add worker data from commented down below
        if (pthread_create(&workers[i], NULL, stream, NULL))
        {
            fprintf(stderr, "Error spawning worker\n");
        }
    }
    // CONTROL THREAD
    do
    {
        usleep(100);
    } while (++e_time != c_time);
    for (i = 0; i < streams; i++)
        pthread_join(workers[i], NULL);

    /*
    free(net_buffer);
    free(udp_serv_addr);
    free(udp_self_addr);
    free(udp_sockfd);
    free(workers);
    */
    return;
    // --------------------------------------
    // bandwidth = threads * packets_per_sec * packet_size
    // packets_per_sec = bandwidth/(threads*packet_size)
    // interval = 1/packets_per_sec

    // time1
    // send
    // time2
    // delta = time2-time1
    // sleep (interval-delta)
    // busy time = delta/interval

    // unsigned int packets_per_sec = bandwidth / (streams * udp_packet_size);
    // float interval = 1 / packets_per_sec, sleep_time;
    // //    float interval = ((float)(streams*udp_packet_size*8))/bandwidth;
    // pthread_t worker, *workers;

    // bzero(&h, sizeof(Header));
    // memcpy(&(h.id), "netperf", strlen("netperf") + 1);
    // h.type = htons(0x01);
    // h.length = htons(udp_packet_size);
    // h.header_fin = htons(MAGIC_16);

    // net_bufer_size = sizeof(uint8_t) * udp_packet_size;
    // net_buffer = realloc(net_buffer, net_bufer_size);
    // tmp = net_buffer;
    // size = sizeof(Header);
    // memcpy(tmp, &h, size);
    // tmp += size;

    // size = sizeof(uint8_t) * udp_packet_size - sizeof(Header) - sizeof(MAGIC_16);
    // rnd_fd = open("/dev/urandom", O_RDONLY);
    // read(rnd_fd, tmp, size);
    // close(rnd_fd);
    // tmp += size;
    // *(uint16_t *)tmp = htons(MAGIC_16);
}

void *stream(void *data)
{
    usleep(100);
    printf("STREAM!\n");
    //     struct timeval init, start, end;
    //     short stop = 0;
    //     int msg_count = 0, msg_received_count = 0, flag = 1;
    //     clock_t begin = clock();
    //     //    gettimeofday(&init, NULL);
    //     //    printf("%ld.%06ld\n", init.tv_sec, init.tv_usec);

    //     while (1)
    //     {
    //         if (stop)
    //             break;

    //         for (int i = 0; i < packets_per_sec; i++)
    //         {
    //             gettimeofday(&start, NULL);
    //             if (sendto(udp_sockfd, net_buffer, net_bufer_size, 0, (struct sockaddr *)&udp_serv_addr,
    //                        sizeof(udp_serv_addr)) <= 0)
    //             {
    //                 flag = 0;
    //             }
    //             else
    //             {
    //                 flag = 1;
    //             }
    //             gettimeofday(&end, NULL);
    //             sleep_time = interval - timedifference_msec(start, end);
    //             if (sleep_time < 0)
    //                 sleep_time = 0;
    //             sleep(sleep_time);
    //         }
    //         clock_t end_ = clock();
    //         double time_spent = (double)(end_ - begin) / CLOCKS_PER_SEC;
    //         if (flag)
    //         {
    //             printf(" msg_seq=%d time=%f\n", msg_count, time_spent);
    //             msg_received_count++;
    //         }
    //         else
    //             printf("\nPacket sending failed!\n");
    //         msg_count++;
    //     }
    //     clock_t ending = clock();
    //     double time_spent = (double)(ending - begin) / CLOCKS_PER_SEC;
    //     printf("\n%d packets sent, %d packets received, %f percent packet loss. Total time: %f ms.\n\n", msg_count, msg_received_count, ((msg_count - msg_received_count) / msg_count) * 100.0, time_spent);
}