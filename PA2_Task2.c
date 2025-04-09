/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/*
Please specify the group members here
# Student #1: Jonathan Stilz
# Student #2:
# Student #3:
*/
/*Utilized Chat-GPT-4 Prompt "gettimeofday function in C"
Was also used to understand epoll and socket relationships and troubleshoot errors
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
// #include <sys/timerfd.h>
// #include <time.h>

#define MAX_EVENTS 64
#define PACKET_SIZE 32
#define DEFAULT_CLIENT_THREADS 4
#define MAX_PKT 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;
pthread_mutex_t request_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct
{
    // int id; //FOR DEBUGGING ONLY
    int epoll_fd;  /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd; /* File descriptor for the client socket connected to the server. */
    struct sockaddr_in server_addr, client_addr;
    long long total_rtt;      /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages;      /* Total number of messages sent and received. */
    long total_messages_sent; /* Total number of messages sent. */
    long total_messages_recv; /* Total number of messages received. */
    float request_rate;       /* Computed request rate (requests per second) based on RTT and total messages. */
    uint8_t *ack_packets;
    volatile int timer_expired;

} client_thread_data_t;

typedef unsigned int seq_nr; /*sequence numbers*/
typedef struct
{
    unsigned char data[MAX_PKT * 4];
} packet;
typedef enum
{
    data,
    ack
} frame_kind;

typedef struct
{

    frame_kind kind;
    seq_nr seq; /*What kind of frame*/
    packet info;

} frame;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
// pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
void *client_thread_func(void *arg)
{

    client_thread_data_t *data = (client_thread_data_t *)arg;
    data->total_rtt = 0;
    data->total_messages = 0;
    data->total_messages_sent = 0;
    data->total_messages_recv = 0;
    data->request_rate = 0;
    data->timer_expired = 0;

    struct epoll_event event, events[MAX_EVENTS];
    // char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    // define frame to send
    frame send_frame;

    send_frame.kind = (frame_kind)ack;
    packet p;
    // p.data = "ABCDEFGHIJKMLNOP";
    strncpy((char *)p.data, "ABCDEFGHIJKMLNOP", MAX_PKT * 4);
    send_frame.info = p;
    // seq number needs to be defined later
    send_frame.seq = 0;
    // holder for frame to be received
    frame recv_frame;
    // char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;
    // debug
    int sent = 0;
    int rec = 0;

    // Hint 1: register the "connected" client_thread's socket in the its epoll instance
    // Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.

    /* TODO:
     * It sends messages to the server, waits for a response using epoll,
     * and measures the round-trip time (RTT) of this request-response.
     */
    // ensure only one event at a time received

    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) == -1)
    {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }
    gettimeofday(&start, NULL);
    // Wait for socket to be writable (connection established)

    // ERROR: Doesn't like for loops?
    // printf("I run before the for loop!\n");
    int counter2 = 0;
    while (num_requests)
    { // Distribute requests across threads
        // printf("I run at the beginning of the for loop!\n");
        pthread_mutex_lock(&request_mutex); // Lock before checking/modifying
        if (num_requests == 0)
        {
            pthread_mutex_unlock(&request_mutex);
            break; // Exit when all requests are processed
        }
        num_requests--;                // Safe decrement
        send_frame.seq = num_requests; // update sequence
        // printf("Currently %i messages left\n", num_requests); //debug
        pthread_mutex_unlock(&request_mutex); // Unlock after modification

        // send until acknowledged
        char acknowledged = 0;
        int counter = 0;
        counter2++;
        while (!acknowledged)
        {
            // debug
            
            if (counter)
            {
                printf("current count! %d \n", counter);
            }
            counter++;
            

            socklen_t addrlen = sizeof(data->client_addr);
            //printf("Sending frame %i\n", send_frame.seq);
            // SEND FRAME
            sendto(data->socket_fd, &send_frame, PACKET_SIZE, 0, (struct sockaddr *)&data->server_addr, addrlen);
            data->total_messages++;
            data->total_messages_sent++;

            // Wait for response
            int num_events = epoll_wait(data->epoll_fd, events, 1, 1);
            if (num_events > 0)
            {

                int bytes_received = recvfrom(data->socket_fd, &recv_frame, PACKET_SIZE, 0, (struct sockaddr *)&data->client_addr, &addrlen);
                // usleep(10000); // 10ms delay

                // received packet
                // debug
                // printf("%i\n", send_frame.seq);
                // printf("%i\n", recv_frame.seq);
                
                if (bytes_received > 0 && recv_frame.seq == send_frame.seq)
                {
                    recv_frame.info.data[bytes_received] = '\0';
                    //printf("Received packet %i server: %s\n", recv_frame.seq, recv_frame.info.data);
                    data->total_messages++;
                    data->total_messages_recv++;
                    // continue to next packet
                    acknowledged = 1;
                }
                    
            }
        }
    }

    /* TODO:
     * The function exits after sending and receiving a predefined number of messages (num_requests).
     * It calculates the request rate based on total messages and RTT
     */
    gettimeofday(&end, NULL);

    long long seconds = end.tv_sec - start.tv_sec;
    long long microseconds = end.tv_usec - start.tv_usec;
    data->total_rtt = seconds * 1000000 + microseconds;

    // debug
    // printf("Seconds: %lld\n", seconds);
    // printf("Microseconds: %lld\n", microseconds);
    // printf("Total Ms: %ld\n", data->total_messages);

    // printf("Total Ms Sent: %i\n", sent);
    // printf("Total Ms Rec: %i\n", rec);

    data->request_rate = (data->total_messages / (seconds + ((float)microseconds / 1000000)));

    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client()
{
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;
    uint8_t ack_packets[num_requests];
    memset(ack_packets, 0, sizeof(ack_packets));

    /* TODO:
     * Create sockets and epoll instances for client threads
     * and connect these sockets of client threads to the server
     */

    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++)
    {

        int socket_fd, epoll_fd;
        // char buffer[MESSAGE_SIZE];

        // Create socket
        if ((socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        // Set socket to non-blocking
        int flags = fcntl(socket_fd, F_GETFL, 0);
        fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        server_addr.sin_addr.s_addr = inet_addr(server_ip);
        // pthread_mutex_init(&lock, NULL);
        //  Connect to the server
        if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        {
            if (errno != EINPROGRESS)
            {
                perror("connect");
                exit(EXIT_FAILURE);
            }
        }

        // create epoll
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1)
        {
            perror("epoll_create");
            exit(EXIT_FAILURE);
        }

        thread_data[i].epoll_fd = epoll_fd;
        thread_data[i].socket_fd = socket_fd;
        thread_data[i].server_addr = server_addr;
        thread_data[i].ack_packets = ack_packets;
        // for debug
        // thread_data[i].id = i;
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    /* TODO:
     * Wait for client threads to complete and aggregate metrics of all client threads
     */

    // Wait for threads to complete

    for (int i = 0; i < num_client_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }
    // printf("I made it!\n");

    long long total_rtt = 0; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    for (int i = 0; i < num_client_threads; i++)
    {
        total_rtt += thread_data[i].total_rtt;
    }

    long total_messages = 0; /* Total number of messages sent and received. */
    long total_messages_sent = 0;
    long total_messages_recv = 0;
    float total_request_rate = 0; /* Computed request rate (requests per second) based on RTT and total messages. */
    for (int i = 0; i < num_client_threads; i++)
    {
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate / num_client_threads;
        total_messages_recv += thread_data[i].total_messages_recv;
        total_messages_sent += thread_data[i].total_messages_sent;
    }

    for (int i = 0; i < num_client_threads; i++)
    {
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }
    float packet_loss = 100 * (1 - (float)total_messages_recv / total_messages_sent);
    // Debug
    // printf("Total RTT: %lld us\n", total_rtt);
    // printf("Total Msgs: %lld us\n", total_messages);

    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
    printf("Total Packets Sent: %ld\n", total_messages_sent);
    printf("Total Packets Received: %ld\n", total_messages_recv);
    printf("Percentage of Packet Loss: %f\n", packet_loss);
}

// pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
void run_server()
{
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    struct epoll_event event, events[MAX_EVENTS];
    frame recv_packet;

    /* TODO:
     * Server creates listening socket and epoll instance.
     * Server registers the listening socket to epoll
     */

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set socket to non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    // Add the server socket to epoll
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1)
    {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    //simulate small buffer
    int buffer_size = 512;  // Set a small buffer size, e.g., 512 bytes
    setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));

    /* Server's run-to-completion event loop */
    while (1)
    {
        /* TODO:
         * Server uses epoll to handle connection establishment with clients
         * or receive the message from clients and echo the message back
         */
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < num_events; i++)
        {
            // for TCP
            //printf("I'm kinda in! %i\n",num_events);
            if (events[i].events)
            {
                //printf("I'm in!\n");
                //  Read from the client socket
                socklen_t addrlen = sizeof(client_addr);
                int bytes_read = recvfrom(events[i].data.fd, &recv_packet, PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addrlen);

                if (bytes_read <= 0)
                {
                    printf("I quit!!\n");
                    // If client disconnects or error occurs, remove the socket from epoll
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                }
                else
                {
                    // Respond to client
                    recv_packet.info.data[bytes_read] = '\0';
                    printf("Received SN %i: %s\n", recv_packet.seq, recv_packet.info.data);

                    // update type of packet
                    recv_packet.kind = ack;
                    sendto(events[i].data.fd, &recv_packet, PACKET_SIZE, 0, (struct sockaddr *)&client_addr, addrlen);
                    // for TCP
                    // write(events[i].data.fd, buffer, MESSAGE_SIZE); // Send a response
                }
            }
        }
        //printf("I'm done!\n");
    }
    close(server_fd);
    close(epoll_fd);
}
int main(int argc, char *argv[])
{
    // printf("I run at the beginning of main!\n");
    if (argc > 1 && strcmp(argv[1], "server") == 0)
    {
        if (argc > 2)
            server_ip = argv[2];
        if (argc > 3)
            server_port = atoi(argv[3]);

        run_server();
    }
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
    {
        if (argc > 2)
            server_ip = argv[2];
        if (argc > 3)
            server_port = atoi(argv[3]);
        if (argc > 4)
            num_client_threads = atoi(argv[4]);
        if (argc > 5)
            num_requests = atoi(argv[5]);
        // printf("I run before run_client!\n");
        run_client();
    }
    else
    {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
