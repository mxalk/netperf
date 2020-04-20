//
// Created by manos on 3/27/20.
//

#include "netperf.h"

extern char *human_formats[];
extern char *boolean_str[];

typedef struct threadData
{
    int udp_sockfd;
    struct sockaddr_in udp_serv_addr;
    pthread_t *self;

    long unsigned interval, *packets_sent;
    uint8_t delay_mode, stream_id;
    uint16_t udp_packet_size;
    pthread_mutex_t *mutex;
    unsigned short *run, *throttled;

    size_t net_buffer_size;
    uint16_t *net_buffer;
    // uint64_t *c_time; // workers should be time-unaware. controlled from main thread

} ThreadData;

int msg_count = 0, msg_received_count = 0, flag = 1;

void client(uint16_t udp_packet_size, uint64_t bandwidth, uint16_t streams, uint64_t c_time, uint8_t delay_mode, uint16_t wait, FILE *f, int f_open)
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
    char *ptr, buffer[512];
    unsigned long packets_sent = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    unsigned short run = 1, throttled = 0;

    fprintf(f_open?f:stdout, "------------------------------------\n");
    fprintf(f_open?f:stdout, "-------------- CLIENT --------------\n");
    fprintf(f_open?f:stdout, "------------------------------------\n");
    fprintf(f_open?f:stdout, "UDP Packet size: %u\n", udp_packet_size);
    fprintf(f_open?f:stdout, "Bandwidth: %lu\n", bandwidth);
    fprintf(f_open?f:stdout, "Parallel streams(threads): %u\n", streams);
    fprintf(f_open?f:stdout, "Experiment Time: %lus\n", c_time);
    fprintf(f_open?f:stdout, "One Way delay mode: %s\n", boolean_str[delay_mode]);
    fprintf(f_open?f:stdout, "Wait time: %u\n", wait);
    fprintf(f_open?f:stdout, "------------------------------------\n");

    // CREATE TCP SOCKET
    bzero(&tcp_self_addr, sizeof(struct sockaddr_in));
    tcp_self_addr.sin_family = AF_INET;
    tcp_self_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_self_addr.sin_port = htons(0);
    if (bind(tcp_sockfd, (struct sockaddr *)&tcp_self_addr, sizeof(tcp_self_addr)) == -1)
        error("Socket bind failed", 2);
    if (connect(tcp_sockfd, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)) == -1)
        error("Connection to server failed", 2);

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
        memcpy(&(udp_serv_addr[i]), &tcp_server_addr, sizeof(struct sockaddr_in));
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
        memcpy(udp_self_addr + i, &tcp_self_addr, sizeof(struct sockaddr_in));
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
    long unsigned interval = 1000000 / packets_per_sec, sleep_time;
    //    float interval = ((float)(streams*udp_packet_size*8))/bandwidth;
    pthread_t *workers;

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
        data[i].packets_sent = &packets_sent;
        data[i].mutex = &mutex;
        data[i].run = &run;
        data[i].throttled = &throttled;
        // on error, probably should exit
        if (pthread_create(workers + i, NULL, stream_sender, data + i))
            fprintf(stderr, "Error spawning worker\n");
    }
    fprintf(f_open?f:stdout, "------------------------------------\n");

    if (packets_per_sec < 0.5)
    {
        fprintf(f_open?f:stdout, "WARNING! Extremely low packets per second (%.2f)\n", packets_per_sec);
        fprintf(f_open?f:stdout, "         Packet loss will produce error!\n");
    }
    else if (packets_per_sec < 1)
    {
        fprintf(f_open?f:stdout, "WARNING! Low packets per second (%.2f)\n", packets_per_sec);
    }

    if (wait)
    {
        fprintf(f_open?f:stdout, "Introducing the delay of %u seconds before the measurement begins.\n", wait);
        sleep(wait);
    }
    fprintf(f_open?f:stdout, "------------------------------------\n");
    // RELEASE BARRIER
    unsigned long throughput, goodput, packets_arrived;
    float packet_loss;
    // CONTROL THREAD
    do
    {
        // receive tcp non block data. print once every <print_interval> time
        // in case of stop signal, notify server -- handle sigint
        recv(tcp_sockfd, net_buffer, net_buffer_size, 0); // netbuffer could be too small?
        throughput = strtoul((char *)net_buffer, &ptr, 10);
        ptr++;
        goodput = strtoul(ptr, &ptr, 10);
        if (strlen(ptr))
            continue;
        strcpy((char *)net_buffer, "0");
        fprintf(f_open?f:stdout, "\t%10s", "THROUGHPUT: ");
        print_human_format(throughput, f, f_open);
        fprintf(f_open?f:stdout, "\t%10s", "GOODPUT: ");
        print_human_format(goodput, f, f_open);
        if (throttled)
        {
            fprintf(f_open?f:stdout, "\tthrottling!!");
            throttled = 0;
        }
        fprintf(f_open?f:stdout, "\n");
        // usleep(100);
    } while (++e_time != c_time); // e_time should be clock computed and not inc'ed
    // polling based solution above should be fine.
    fprintf(f_open?f:stdout, "Waiting for streams to finish...\n");
    run = 0;
    for (i = 0; i < streams; i++)
        pthread_join(workers[i], NULL);
    fprintf(f_open?f:stdout, "Finished.\n");

    // send STOP signal
    net_buffer_size = sizeof(uint16_t);
    net_buffer = realloc(net_buffer, net_buffer_size);
    *net_buffer = htons(MAGIC_16);
    send(tcp_sockfd, net_buffer, net_buffer_size, 0);
    fprintf(f_open?f:stdout, "STOP signal sent\n");

    net_buffer_size = sizeof(uint32_t);
    net_buffer = realloc(net_buffer, net_buffer_size);
    recv(tcp_sockfd, net_buffer, net_buffer_size, 0);
    packets_arrived = ntohl(*((uint32_t *)net_buffer));
    fprintf(f_open?f:stdout, "Packets sent: %lu\nPackets arrived: %lu\n", packets_sent, packets_arrived);
    packet_loss = 100 - (100 * packets_arrived / (float)packets_sent);
    fprintf(f_open?f:stdout, "\t%16s %6.2f%%", "PACKET LOSS: ", packet_loss);
    if (packet_loss)
        fprintf(f_open?f:stdout, " (try increaseing threads?)");
    fprintf(f_open?f:stdout, "\n");

    free(net_buffer);
    free(udp_serv_addr);
    free(udp_self_addr);
    free(udp_sockfd);
    free(workers);
    free(data);
}

void *stream_sender(void *data)
{
    ThreadData *threadData = (ThreadData *)data;
    ssize_t size;
    struct timeval init, start, end;

    // printf("STREAM %u\n", threadData->stream_id);

    // create barrier to start simultaneously across streams
    gettimeofday(&init, NULL);
    while (*(threadData->run))
    {
        // printf("STREAM %u sending\n", threadData->stream_id);
        gettimeofday(&start, NULL);
        // printf("S %ld.%06ld\n", start.tv_sec, start.tv_usec);
        size = sendto(threadData->udp_sockfd, threadData->net_buffer, threadData->net_buffer_size, 0, (struct sockaddr *)&threadData->udp_serv_addr, sizeof(threadData->udp_serv_addr));
        // if (size != threadData->net_buffer_size)
        //     printf("STREAM %u sent less\n", threadData->stream_id);
        pthread_mutex_lock(threadData->mutex);
        *(threadData->packets_sent) += 1;
        pthread_mutex_unlock(threadData->mutex);
        gettimeofday(&end, NULL);
        // printf("E %ld.%06ld\n", end.tv_sec, end.tv_usec);
        // printf("E %ld.%06ld\n", end.tv_sec-start.tv_sec, end.tv_usec-start.tv_usec);
        // printf("%lu %lu\n", threadData->interval, timedifference_usec(start, end));
        if (threadData->interval >= timedifference_usec(start, end))
        {
            usleep(threadData->interval - timedifference_usec(start, end));
        }
        else
            *(threadData->throttled) = 1;
    }
    // printf("Stream %u exiting\n", threadData->stream_id);
}

void print_human_format(unsigned long bytes, FILE *f, int f_open)
{
    double eng_format = bytes;
    unsigned power = 0;
    while (eng_format >= 1024) // does not enter if ==
    {
        power++;
        eng_format /= 1024;
    }
    fprintf(f_open?f:stdout, "%7.2f%sB/s ", eng_format, human_formats[power]);
    eng_format *= 8;
    if (eng_format > 1024)
    {
        power++;
        eng_format /= 1024;
    }
   fprintf(f_open?f:stdout, "%7.2f%sbps", eng_format, human_formats[power]);
}