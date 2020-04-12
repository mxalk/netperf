//
// Created by manos on 3/27/20.
//

#include "netperf.h"

extern char *boolean_str[];

typedef struct threadData
{
    int udp_sockfd;
    struct sockaddr_in udp_serv_addr;
    pthread_t *self;

    float interval;
    uint8_t delay_mode, stream_id;
    uint16_t udp_packet_size;

    size_t net_buffer_size;
    uint16_t *net_buffer;
    // uint64_t *c_time; // workers should be time-unaware. controlled from main thread

} ThreadData;

int msg_count = 0, msg_received_count = 0, flag = 1;

void client(uint16_t udp_packet_size, uint64_t bandwidth, uint8_t streams, uint64_t c_time, uint8_t delay_mode, uint16_t wait)
{
    struct sockaddr_in tcp_self_addr, *udp_self_addr, *udp_serv_addr;
    int *udp_sockfd, rnd_fd, i;
    socklen_t len;
    uint8_t *tmp;
    size_t size;
    uint64_t e_time = 0;
    ThreadData *data;
    size_t net_buffer_size;
    uint16_t *net_buffer;

    printf("---------------- CLIENT ----------------\n");
    printf("UDP Packet size: %u\n", udp_packet_size);
    printf("Bandwidth: %lu\n", bandwidth);
    printf("Parallel streams(threads): %u\n", streams);
    printf("Experiment Time: %lus\n", c_time);
    printf("One Way delay mode: %s\n", boolean_str[delay_mode]);
    printf("Wait time: %u\n", wait);

    // CREATE TCP SOCKET
    bzero(&tcp_self_addr, sizeof(struct sockaddr_in));
    tcp_self_addr.sin_family = AF_INET;
    tcp_self_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_self_addr.sin_port = htons(0);
    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_self_addr, sizeof(tcp_self_addr)) == -1)
        error("Socket bind failed.\n", 2);
    if (connect(tcp_sockfd, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)) == -1)
        error("Connection to server failed.\n", 2);

    // 1. send udp_packet_size, # streams, delay_mode
    // BUFFERS
    net_buffer_size = sizeof(uint16_t) * 3;
    net_buffer = malloc(net_buffer_size);

    // send to server through tcp
    net_buffer[0] = htons(udp_packet_size);
    net_buffer[1] = htons(streams);
    net_buffer[2] = htons(delay_mode);
    send(tcp_sockfd, net_buffer, net_buffer_size, 0);

    // 4. receive #streams udp server addresses.
    net_buffer_size = sizeof(uint16_t) * streams;
    net_buffer = realloc(net_buffer, net_buffer_size);

    udp_serv_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_self_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_sockfd = malloc(sizeof(int) * streams);

    bzero(udp_serv_addr, sizeof(struct sockaddr_in) * streams);
    bzero(udp_self_addr, sizeof(struct sockaddr_in) * streams);

    recv(tcp_sockfd, net_buffer, net_buffer_size, 0);

    // REMOTE UDP SOCKETS
    for (i = 0; i < streams; i++)
    {
        // setup server socket
        memcpy(&tcp_server_addr, &(udp_serv_addr[i]), sizeof(struct sockaddr_in));
        udp_serv_addr[i].sin_port = net_buffer[i];
    }
    printf("Remote UDP: %u", ntohs(udp_serv_addr[0].sin_port));
    for (i = 1; i < streams; i++)
        printf(", %u", ntohs(udp_serv_addr[i].sin_port));
    printf(".\n");
    // SELF UDP SOCKETS
    for (i = 0; i < streams; i++)
    {
        // setup self socket
        memcpy(&tcp_self_addr, udp_self_addr + i, sizeof(struct sockaddr_in));
        udp_self_addr[i].sin_port = htons(0);
        if ((udp_sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            error("Socket", 2);
        if (bind(udp_sockfd[i], (struct sockaddr *)(udp_self_addr + i), sizeof(struct sockaddr)) == -1)
            error("Bind", 2);
        len = sizeof(udp_self_addr[i]);
        if (getsockname(udp_sockfd[i], (struct sockaddr *)(udp_self_addr + i), &len) == -1)
            error("Getsockname", 2);
    }
    printf("Self UDP: %u", ntohs(udp_self_addr[0].sin_port));
    for (i = 1; i < streams; i++)
        printf(", %u", ntohs(udp_self_addr[i].sin_port));
    printf(".\n");

    // --------------------------------------
    // bandwidth = threads * packets_per_sec * packet_size
    // packets_per_sec = bandwidth/(threads*packet_size)
    // interval = 1/packets_per_sec = seconds/packet
    // time1
    // send
    // time2
    // delta = time2-time1
    // sleep (interval-delta) // time_per_packet - time it took = time to wait
    // busy time = delta/interval

    float packets_per_sec = bandwidth / (float)(streams * udp_packet_size);
    float interval = 1000000 / (float)packets_per_sec, sleep_time;
    //    float interval = ((float)(streams*udp_packet_size*8))/bandwidth;
    pthread_t *workers;
    printf("%f\n", interval);

    workers = malloc(sizeof(pthread_t) * streams);
    data = malloc(sizeof(ThreadData) * streams);
    net_buffer_size = sizeof(uint8_t) * udp_packet_size;
    net_buffer = realloc(net_buffer, net_buffer_size);
    rnd_fd = open("/dev/urandom", O_RDONLY);
    read(rnd_fd, net_buffer, net_buffer_size);
    close(rnd_fd);
    memcpy(net_buffer, "netperf", strlen("netperf") + 1);
    for (i = 0; i < streams; i++)
    {
        data[i].udp_sockfd = udp_sockfd[i];
        data[i].udp_serv_addr = udp_serv_addr[i];
        data[i].interval = interval;
        data[i].udp_packet_size = udp_packet_size;
        data[i].delay_mode = delay_mode;
        data[i].stream_id = i;
        data[i].self = workers + i;
        data[i].net_buffer = net_buffer;
        data[i].net_buffer_size = net_buffer_size;
        // on error, probably should exit
        if (pthread_create(workers + i, NULL, stream_sender, data + i))
            fprintf(stderr, "Error spawning worker\n");
    }

    if (wait)
    {
        printf("Introducing the delay of %u seconds before the measurement begins.\n", wait);
        sleep(wait);
    }
    // RELEASE BARRIER

    // CONTROL THREAD
    do
    {
        // receive tcp non block data. print once every <print_interval> time
        // in case of stop signal, notify server -- handle sigint
        usleep(100);
    } while (++e_time != c_time); // e_time should be clock computed and not inc'ed
    // polling based solution above should be fine.
    for (i = 0; i < streams; i++)
        pthread_join(workers[i], NULL);

    free(net_buffer);
    free(udp_serv_addr);
    free(udp_self_addr);
    free(udp_sockfd);
    free(workers);
    return;
}

void *stream_sender(void *data)
{
    ThreadData *threadData = (ThreadData *)data;
    ssize_t size;
    unsigned short stop = 0;

    printf("STREAM %u\n", threadData->stream_id);

    // create barrier to start simultaneously across streams
    while (1)
    {
        if (stop)
            break;
        printf("STREAM %u sending\n", threadData->stream_id);
        size = sendto(threadData->udp_sockfd, threadData->net_buffer, threadData->net_buffer_size, 0, (struct sockaddr *)&threadData->udp_serv_addr, sizeof(threadData->udp_serv_addr));
        if (size != threadData->net_buffer_size)
            printf("STREAM %u sent less\n", threadData->stream_id);
        usleep(threadData->interval);
    }

    if (size <= 0)
    {
        flag = 0;
    }
    else
    {
        flag = 1;
    }
    // struct timeval init, start, end;
    // int msg_count = 0, msg_received_count = 0, flag = 1;
    // clock_t begin = clock();
    // //    gettimeofday(&init, NULL);
    // //    printf("%ld.%06ld\n", init.tv_sec, init.tv_usec);

    // while (1)
    // {
    //     if (stop)
    //         break;

    //     for (int i = 0; i < packets_per_sec; i++)
    //     {
    //         gettimeofday(&start, NULL);
    //         if (sendto(udp_sockfd, net_buffer, net_buffer_size, 0, (struct sockaddr *)&udp_serv_addr,
    //                    sizeof(udp_serv_addr)) <= 0)
    //         {
    //             flag = 0;
    //         }
    //         else
    //         {
    //             flag = 1;
    //         }
    //         gettimeofday(&end, NULL);
    //         sleep_time = interval - timedifference_msec(start, end);
    //         if (sleep_time < 0)
    //             sleep_time = 0;
    //         sleep(sleep_time);
    //     }
    //     clock_t end_ = clock();
    //     double time_spent = (double)(end_ - begin) / CLOCKS_PER_SEC;
    //     if (flag)
    //     {
    //         printf(" msg_seq=%d time=%f\n", msg_count, time_spent);
    //         msg_received_count++;
    //     }
    //     else
    //         printf("\nPacket sending failed!\n");
    //     msg_count++;
    // }
    // clock_t ending = clock();
    // double time_spent = (double)(ending - begin) / CLOCKS_PER_SEC;
    // printf("\n%d packets sent, %d packets received, %f percent packet loss. Total time: %f ms.\n\n", msg_count, msg_received_count, ((msg_count - msg_received_count) / msg_count) * 100.0, time_spent);
}