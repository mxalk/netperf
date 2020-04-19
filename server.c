//
// Created by manos on 3/27/20.
//

#include "netperf.h"

extern char *boolean_str[];

typedef struct threadData
{
    int udp_sock_fd;
    uint8_t delay_mode;
    uint16_t udp_packet_size;

    unsigned long *throughput, *goodput;
    uint32_t *packets;
    pthread_mutex_t *mutex;
    uint8_t *net_buffer;
    size_t net_buffer_size;
    unsigned short *run;

    struct sockaddr_in *client_addr; // can be removed unless checking receiving data after client port negotiation
} ThreadData;

void server()
{
    pthread_t *worker;
    Inc_Connection *remote;
    struct sockaddr_in client_addr;
    socklen_t len;
    int sockfd;

    printf("------------------------------------\n");
    printf("-------------- SERVER --------------\n");
    printf("------------------------------------\n");

    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)) == -1)
        error("Bind", 2);
    if (listen(tcp_sockfd, 5) == -1)
        error("Listen", 2);
    bzero(&client_addr, sizeof(struct sockaddr_in));
    while (1)
    {
        len = sizeof(struct sockaddr_in);
        sockfd = accept(tcp_sockfd, (struct sockaddr *)&client_addr, &len);
        if (sockfd == -1)
        {
            fprintf(stderr, "Accept error!\n");
            continue;
        }
        printf("Incoming connection from '%s' port '%u'\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        remote = malloc(sizeof(Inc_Connection));
        remote->sockfd = sockfd;
        remote->address = client_addr;
        remote->len = len;
        worker = malloc(sizeof(pthread_t));
        remote->self = worker;
        if (pthread_create(worker, NULL, handle_inc, remote))
        {
            fprintf(stderr, "Error spawning worker.\n");
            free(worker);
            free(remote);
        }
    }
    printf("Server closing...\n");
}

void *handle_inc(void *data)
{
    struct sockaddr_in *udp_self_addr, from_addr;
    char buffer[1024];
    int *udp_sockfd, tmp;
    unsigned short i, run = 1;
    socklen_t len;
    Inc_Connection *conn = (Inc_Connection *)data;
    size_t net_buffer_size;
    uint16_t *net_buffer, streams, delay_mode = 0, udp_packet_size;
    ThreadData *tdata;
    unsigned long throughput = 0, goodput = 0, throughtput_bk, goodput_bk, packets_bk;
    uint32_t packets = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    printf("Worker handling connection from '%s' port '%u'\n", inet_ntoa(conn->address.sin_addr), ntohs(conn->address.sin_port));

    // 2. receive udp_packet_size, #streams, delay_mode
    // BUFFERS
    net_buffer_size = sizeof(uint16_t) * 3;
    net_buffer = malloc(net_buffer_size);

    // send from client through tcp
    recv(conn->sockfd, net_buffer, net_buffer_size, 0);
    udp_packet_size = ntohs(net_buffer[0]);
    streams = ntohs(net_buffer[1]);
    delay_mode = ntohs(net_buffer[2]);
    printf("Client udp_packet_size: %u. streams: %u. delay_mode: %s\n", udp_packet_size, streams, boolean_str[delay_mode]);

    // 3. setup #streams udp sockets, and send to client
    net_buffer_size = sizeof(uint16_t) * streams;
    net_buffer = realloc(net_buffer, net_buffer_size);

    udp_self_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_sockfd = malloc(sizeof(int) * streams);

    bzero(udp_self_addr, sizeof(struct sockaddr_in) * streams);
    pthread_t *workers;
    workers = malloc(sizeof(pthread_t) * streams);
    tdata = malloc(sizeof(ThreadData) * streams);
    for (i = 0; i < streams; i++)
    {
        memcpy(udp_self_addr + i, &tcp_server_addr, sizeof(struct sockaddr_in));
        udp_self_addr[i].sin_port = htons(0);
        if ((udp_sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            error("Socket", 2);
        if (bind(udp_sockfd[i], (struct sockaddr *)(udp_self_addr + i), sizeof(struct sockaddr)) == -1)
            error("Bind", 2);
        len = sizeof(udp_self_addr[i]);
        if (getsockname(udp_sockfd[i], (struct sockaddr *)(udp_self_addr + i), &len) == -1)
            error("Getsockname", 2);
        fcntl(udp_sockfd[i], F_SETFL, O_NONBLOCK);
        // printf("UDP open, port '%u'\n", ntohs(udp_self_addr[i].sin_port));
        net_buffer[i] = udp_self_addr[i].sin_port;
        // here should be broken to one udp_self_addr per thread
        // WORKER
        tdata[i].udp_sock_fd = udp_sockfd[i];
        tdata[i].throughput = &throughput;
        tdata[i].goodput = &goodput;
        tdata[i].packets = &packets;
        tdata[i].mutex = &mutex;
        tdata[i].run = &run;
        tdata[i].client_addr = &conn->address;
        tdata[i].net_buffer_size = sizeof(uint8_t) * udp_packet_size;
        tdata[i].net_buffer = malloc(tdata[i].net_buffer_size);
        if (pthread_create(workers + i, NULL, stream_receiver, tdata + i))
            fprintf(stderr, "Error spawning worker\n");
    }
    printf("UDP ports: %u", ntohs(udp_self_addr[0].sin_port));
    for (i = 1; i < streams; i++)
        printf(", %u", ntohs(udp_self_addr[i].sin_port));
    printf(".\n");

    //barrier to wait for rtt measurement

    send(conn->sockfd, net_buffer, net_buffer_size, 0);
    // CONTROL THREAD
    fcntl(conn->sockfd, F_SETFL, O_NONBLOCK);
    while (run)
    {
        sleep(1);
        errno = 0;
        recv(conn->sockfd, net_buffer, net_buffer_size, 0);
        if (errno != EWOULDBLOCK && ntohs(*net_buffer) == MAGIC_16)
        {
            printf("STOP signal received\n");
            run = 0;
            break;
        }

        pthread_mutex_lock(&mutex);
        throughtput_bk = throughput;
        throughput = 0;
        goodput_bk = goodput;
        goodput = 0;
        pthread_mutex_unlock(&mutex);
        if (sprintf(buffer, "%lu-%lu", throughtput_bk, goodput_bk) <= 0)
            continue;
        send(conn->sockfd, buffer, strlen(buffer) + 1, 0);
    }
    printf("Waiting for streams to finish...\n");
    for (i = 0; i < streams; i++)
        pthread_join(workers[i], NULL);
    printf("Finished.\n");

    net_buffer_size = sizeof(uint32_t);
    net_buffer = realloc(net_buffer, net_buffer_size);
    *((uint32_t *)net_buffer) = htonl(packets);
    printf("Packets arrived: %lu\n", ntohl(*((uint32_t *)net_buffer)));
    send(conn->sockfd, net_buffer, net_buffer_size, 0);
    // send total packets


    for (i = 0; i < streams; i++) {
        free(tdata[i].net_buffer);
        close(udp_sockfd[i]);
    }
    free(tdata);
    free(workers);
    free(net_buffer);
    free(udp_self_addr);
    free(udp_sockfd);
    close(conn->sockfd);
    free(conn->self);
    free(data);
    return NULL;
}

void *stream_receiver(void *data)
{
    ssize_t size;
    socklen_t len;
    struct sockaddr_in from_addr;
    ThreadData *threadData = (ThreadData *)data;
    // unsigned short run = 1;
    unsigned long throughput, goodput, packets;

    while (*(threadData->run))
    {
        usleep(1000);
        goodput = 0;
        throughput = 0;
        packets = 0;
        errno = 0;
        do
        {
            len = sizeof(struct sockaddr_in);
            size = recvfrom(threadData->udp_sock_fd, threadData->net_buffer, threadData->net_buffer_size, 0, (struct sockaddr *)&from_addr, &len);
            // printf("UDP data from '%s' port '%u' size '%d'\n", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), size);
            if (size > 0)
            {
                goodput += size;
                throughput += size + 46; // add overheads -- ETHERNET 18 - IPV4 20 - UDP 8
                packets += 1;
            }
        } while (errno != EWOULDBLOCK && *(threadData->run));
        if (packets > 0)
        {
            pthread_mutex_lock(threadData->mutex);
            *(threadData->throughput) += throughput;
            *(threadData->goodput) += goodput;
            *(threadData->packets) += packets;
            pthread_mutex_unlock(threadData->mutex);
        }
    }
}