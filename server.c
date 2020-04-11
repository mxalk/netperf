//
// Created by manos on 3/27/20.
//

#include "netperf.h"

void server()
{
    pthread_t *worker;
    Inc_Connection *remote;
    struct sockaddr_in clientname;
    socklen_t len;
    int sockfd;

    printf("---------------- SERVER ----------------\n");
    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)))
        error("Socket bind failed.\n", 2);
    if (listen(tcp_sockfd, 5))
        error("Socket listen failed.\n", 2);
    while (1)
    {
        sockfd = accept(tcp_sockfd, (struct sockaddr *)&clientname, &len);
        if (sockfd < 0)
            continue;
        printf("Incoming connection from '%s' port '%u'\n", inet_ntoa(clientname.sin_addr), ntohs(clientname.sin_port));
        remote = malloc(sizeof(Inc_Connection));
        remote->sockfd = sockfd;
        remote->address = clientname;
        remote->len = len;
        worker = malloc(sizeof(pthread_t));
        remote->self = worker;
        if (pthread_create(worker, NULL, handle_inc, remote))
        {
            fprintf(stderr, "Error spawning worker\n");
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
    Header *h;
    double packets = 0, totalTime = 0, avePacketSize = 0;
    Inc_Connection *conn = (Inc_Connection *)data;
    size_t size, net_buffer_size;
    uint16_t *net_buffer, streams = DEFAULT_STREAMS, delay_mode = 0, udp_packet_size = DEFAULT_UDP_PACKET_SIZE;

    printf("Worker handling connection from '%s' port '%u'\n", inet_ntoa(conn->address.sin_addr), ntohs(conn->address.sin_port));

    // 2. receive udp_packet_size, #streams, delay_mode
    // BUFFERS
    net_buffer_size = sizeof(uint16_t) * 3;
    net_buffer = malloc(net_buffer_size);

    // reveive udp_packet_size through tcp
    size = recv(conn->sockfd, net_buffer, net_buffer_size, 0);
    udp_packet_size = ntohs(net_buffer[0]);
    streams = ntohs(net_buffer[1]);
    delay_mode = ntohs(net_buffer[2]);
    printf("Client udp_packet_size: %u. streams: %u. delay_mode: %u\n", udp_packet_size, streams, delay_mode);

    // 3. setup #streams udp sockets, and send to client
    udp_self_addr = malloc(sizeof(struct sockaddr_in) * streams);
    udp_sockfd = malloc(sizeof(int) * streams);
    len = sizeof(udp_self_addr);
    for (i = 0; i < streams; i++)
    {
        // memset(&(udp_self_addr[i]), 0, sizeof(struct sockaddr_in));
        memcpy(&tcp_server_addr, &(udp_self_addr[i]), sizeof(struct sockaddr_in));
        // udp_self_addr[i].sin_family = AF_INET;
        // udp_self_addr[i].sin_addr.s_addr = tcp_server_addr.sin_addr.s_addr;
        udp_self_addr[i].sin_port = htons(0);
        if ((udp_sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            error("Socket creation failed", 2);
        if (bind(udp_sockfd[i], (struct sockaddr *)&(udp_self_addr[i]), sizeof(udp_self_addr)))
            error("Socket bind failed.\n", 2);
        printf("Binding UDP address to '%s'\n", inet_ntoa(udp_self_addr[i].sin_addr));
        if (getsockname(udp_sockfd[i], (struct sockaddr *)&(udp_self_addr[i]), &len) == -1)
            error("Getsockname\n", 2);
        printf("UDP open, port '%u'\n", ntohs(udp_self_addr[i].sin_port));
        net_buffer[i] = htons(udp_self_addr[i].sin_port);
        // here should be broken to one udp_self_addr per thread
        // WORKER
    }
    send(conn->sockfd, net_buffer, net_buffer_size, 0);
    // CONTROL THREAD

    /*
    free(net_buffer);
    free(udp_self_addr);
    free(udp_sockfd);
    close(conn->sockfd);
    free(conn->self);
    free(data);
    */
    // END
    return NULL;
    // --------------------------------------
    // RECEIVE DATA
    net_buffer_size = sizeof(uint8_t) * udp_packet_size;
    net_buffer = realloc(net_buffer, net_buffer_size);
    do
    {
        size = recvfrom(*udp_sockfd, net_buffer, net_buffer_size, 0, (struct sockaddr *)&from_addr, &len);
        printf("UDP data from '%s' port '%u' size '%zu'\n", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port), size);
        packets += 1;
        avePacketSize += size;
    } while (size);

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    avePacketSize = avePacketSize / packets;
    double goodput = goodPut(packets, avePacketSize, time_spent);
    double throughput = throughput(packets, avePacketSize, time_spent); 
    printf("Average goodPut %f\n", goodput);

    close(udp_sockfd);
    close(conn->sockfd);
    free(conn->self);
    free(data);
    printf("Worker destruction\n");
}

double goodPut(double packets, double avePacketSize, double time)
{
    double headerSize = 32 * packets;
    return (packets*avePacketSize+headerSize))/time;
}

double throughput(double packets, double avePacketSize, double time)
{
    return (packets * avePacketSize) / time;
}
