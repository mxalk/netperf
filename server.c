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

    unsigned long *bytes_recved;
    pthread_mutex_t *mutex;
    uint8_t *net_buffer;
    size_t net_buffer_size;

    struct sockaddr_in *client_addr;
} ThreadData;

void server()
{
    pthread_t *worker;
    Inc_Connection *remote;
    struct sockaddr_in client_addr;
    socklen_t len;
    int sockfd;

    printf("---------------- SERVER ----------------\n");
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
}

void *handle_inc(void *data)
{
    struct sockaddr_in *udp_self_addr, from_addr;
    int *udp_sockfd;
    unsigned short i;
    socklen_t len;
    double packets = 0, totalTime = 0, avePacketSize = 0;
    Inc_Connection *conn = (Inc_Connection *)data;
    size_t size, net_buffer_size;
    uint16_t *net_buffer, streams = DEFAULT_STREAMS, delay_mode = 0, udp_packet_size = DEFAULT_UDP_PACKET_SIZE;
    ThreadData *tdata;
    unsigned long bytes_recved = 0, bytes_recved_bk;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    printf("Worker handling connection from '%s' port '%u'\n", inet_ntoa(conn->address.sin_addr), ntohs(conn->address.sin_port));

    // 2. receive udp_packet_size, #streams, delay_mode
    // BUFFERS
    net_buffer_size = sizeof(uint16_t) * 3;
    net_buffer = malloc(net_buffer_size);

    // send from client through tcp
    size = recv(conn->sockfd, net_buffer, net_buffer_size, 0);
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
        memcpy(&tcp_server_addr, udp_self_addr + i, sizeof(struct sockaddr_in));
        udp_self_addr[i].sin_port = htons(0);
        if ((udp_sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            error("Socket", 2);
        if (bind(udp_sockfd[i], (struct sockaddr *)(udp_self_addr + i), sizeof(struct sockaddr)) == -1)
            error("Bind", 2);
        len = sizeof(udp_self_addr[i]);
        if (getsockname(udp_sockfd[i], (struct sockaddr *)(udp_self_addr + i), &len) == -1)
            error("Getsockname", 2);
        // printf("UDP open, port '%u'\n", ntohs(udp_self_addr[i].sin_port));
        net_buffer[i] = udp_self_addr[i].sin_port;
        // here should be broken to one udp_self_addr per thread
        // WORKER
        tdata[i].udp_sock_fd = udp_sockfd[i];
        tdata[i].bytes_recved = &bytes_recved;
        tdata[i].mutex = &mutex;
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

    send(conn->sockfd, net_buffer, net_buffer_size, 0);
    // CONTROL THREAD

    for (;;)
    {
        pthread_mutex_lock(&mutex);
        bytes_recved_bk = bytes_recved;
        pthread_mutex_unlock(&mutex);
        printf("Total bytes received: %lu\n", bytes_recved_bk);
        sleep(1);
    }

    //    /*
    free(net_buffer);
    free(udp_self_addr);
    free(udp_sockfd);
    close(conn->sockfd);
    free(conn->self);
    free(data);
    //    */
    // END
    return NULL;
    // // --------------------------------------

    // clock_t end = clock();
    // double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    // avePacketSize = avePacketSize / packets;
    // double goodput = goodPut(packets, avePacketSize, time_spent);
    // double throughput = throughput(packets, avePacketSize, time_spent);
    // printf("Average goodPut %f\n", goodput);

    // close(udp_sockfd); // close all
    // close(conn->sockfd);
    // free(conn->self);
    // free(data);
    // printf("Worker destruction\n");
}
void *stream_receiver(void *data)
{
    ssize_t size;
    socklen_t len;
    struct sockaddr_in from_addr;
    ThreadData *threadData = (ThreadData *)data;
    unsigned short stop = 0;

    for(;;)
    {
        if (stop) break;
        len = sizeof(struct sockaddr_in);
        size = recvfrom(threadData->udp_sock_fd, threadData->net_buffer, threadData->net_buffer_size, 0, (struct sockaddr *)&from_addr, &len);
        // printf("UDP data from '%s' port '%u' size '%d'\n", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), size);
        if (size > 0)
        {
            pthread_mutex_lock(threadData->mutex);
            *(threadData->bytes_recved) += size;
            pthread_mutex_unlock(threadData->mutex);
        }
    }
}

// double goodPut(double packets, double avePacketSize, double time)
// {
//     double headerSize = 32 * packets;
//     return (packets*avePacketSize+headerSize))/time;
// }

// double throughput(double packets, double avePacketSize, double time)
// {
//     return (packets * avePacketSize) / time;
// }
