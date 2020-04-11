//
// Created by manos on 3/27/20.
//

#include "netperf.h"

void client()
{
    struct sockaddr_in tcp_self_addr, *udp_self_addr, *udp_serv_addr;
    int *udp_sockfd, rnd_fd, i;
    socklen_t len;
    Header h;
    uint8_t *tmp;
    size_t size;
    uint64_t e_time = 0;

    size_t net_bufer_size;
    uint16_t *net_buffer, streams = DEFAULT_STREAMS, mode = 0, delay_mode = 0, udp_packet_size = DEFAULT_UDP_PACKET_SIZE, port = DEFAULT_PORT;
    uint64_t c_time = DEFAULT_TIME, bandwidth = DEFAULT_BANDWIDTH;

    printf("---------------- CLIENT ----------------\n");

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

    // send udp_packet_size to server through tcp
    net_buffer[0] = htons(udp_packet_size);
    net_buffer[1] = htons(streams);
    net_buffer[2] = htons(delay_mode);
    send(tcp_sockfd, net_buffer, net_bufer_size, 0);

    // 4. receive #streams udp server addresses.
    udp_serv_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_self_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_sockfd = malloc(sizeof(int) * streams);
    net_bufer_size = sizeof(uint16_t) * streams;
    net_buffer = realloc(net_buffer, net_bufer_size);
    recv(tcp_sockfd, net_buffer, net_bufer_size, 0);
    for (i = 0; i < streams; i++) {
        // setup server socket
        memcpy(&tcp_server_addr, (struct sockaddr_in *)&(udp_serv_addr[i]), sizeof(struct sockaddr_in));
        udp_serv_addr[i].sin_port = ntohs(net_buffer[i]);
        printf("Server UDP port: '%u'\n", udp_serv_addr[i].sin_port);
        // setup self socket
        memcpy(&tcp_self_addr, (struct sockaddr_in *)&(udp_self_addr[i]), sizeof(struct sockaddr_in));
        udp_self_addr[i].sin_port = htons(0);
        if (bind(udp_sockfd[i], (struct sockaddr *)&(udp_self_addr[i]), sizeof(struct sockaddr)))
            error("Socket bind failed.\n", 2);
        // here should be broken to one udp_serv_addr per thread
        // WORKER
    }
    // CONTROL THREAD

    // /*
    free(net_buffer);
    free(udp_serv_addr);
    free(udp_self_addr);
    free(udp_sockfd);
    // */
    // END

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
    return;

    unsigned int packets_per_sec = bandwidth / (streams * udp_packet_size);
    float interval = 1 / packets_per_sec, sleep_time;
    //    float interval = ((float)(streams*udp_packet_size*8))/bandwidth;
    pthread_t worker, *workers;

    bzero(&h, sizeof(Header));
    memcpy(&(h.id), "netperf", strlen("netperf") + 1);
    h.type = htons(0x01);
    h.length = htons(udp_packet_size);
    h.header_fin = htons(MAGIC_16);

    net_bufer_size = sizeof(uint8_t) * udp_packet_size;
    net_buffer = realloc(net_buffer, net_bufer_size);
    tmp = net_buffer;
    size = sizeof(Header);
    memcpy(tmp, &h, size);
    tmp += size;

    size = sizeof(uint8_t) * udp_packet_size - sizeof(Header) - sizeof(MAGIC_16);
    rnd_fd = open("/dev/urandom", O_RDONLY);
    read(rnd_fd, tmp, size);
    close(rnd_fd);
    tmp += size;
    *(uint16_t *)tmp = htons(MAGIC_16);

    workers = malloc(sizeof(pthread_t) * streams);
    for (i = 0; i < streams; i++)
    {
        if (pthread_create(&worker, NULL, handle_inc, stream))
        {
            fprintf(stderr, "Error spawning worker\n");
        }
        workers[i] = worker;
    }
    do
    {
        //        printf("Second\n");
        sleep(sleep_time);
    } while (++e_time != c_time);
    for (i = 0; i < streams; i++)
    {
        pthread_join(workers[i], NULL);
    }
    free(workers);
}

void *stream(void *data)
{
    struct timeval init, start, end;
    short stop = 0;
    int msg_count = 0, msg_received_count = 0, flag = 1;
    clock_t begin = clock();
    //    gettimeofday(&init, NULL);
    //    printf("%ld.%06ld\n", init.tv_sec, init.tv_usec);
    
    while (1)
    {
        if (stop)
            break;
        
        for (int i = 0; i < packets_per_sec; i++)
        {
            gettimeofday(&start, NULL);
            if (sendto(udp_sockfd, net_buffer, net_bufer_size, 0, (struct sockaddr *)&udp_serv_addr,
                       sizeof(udp_serv_addr)) <= 0)
            {
                flag = 0;
            }
            else
            {
                flag = 1;
            }
            gettimeofday(&end, NULL);
            sleep_time = interval - timedifference_msec(start, end);
            if (sleep_time < 0)
                sleep_time = 0;
            sleep(sleep_time);
        }
        clock_t end_ = clock();
        double time_spent = (double)(end_ - begin) / CLOCKS_PER_SEC;
        if (flag)
        {
            printf(" msg_seq=%d time=%f\n", msg_count, time_spent);
            msg_received_count++;
        }
        else
            printf("\nPacket sending failed!\n");
        msg_count++;
    }
    clock_t ending = clock();
    double time_spent = (double)(ending - begin) / CLOCKS_PER_SEC;
    printf("\n%d packets sent, %d packets received, %f percent packet loss. Total time: %f ms.\n\n", msg_count, msg_received_count, ((msg_count - msg_received_count) / msg_count) * 100.0, time_spent);

    return NULL;
}
