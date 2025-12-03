#include "rtp.h"
#include "rtpheaders.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define RTP_CLOCK_RATE 90000  // Standard RTP clock rate for video (90 kHz)

// High-level function to send an RTP packet
int send_rtp_packet(int sockfd, struct sockaddr_in *server_addr, unsigned char *payload, int payload_size, int is_last_packet) {
    static struct timeval start_time;
    static int initialized = 0;
    static uint32_t base_timestamp = 0;
    
    // Initialize timing on first call
    if (!initialized) {
        gettimeofday(&start_time, NULL);
        base_timestamp = (uint32_t)(rand() & 0xFFFFFFFF);  // Random initial timestamp
        initialized = 1;
    }
    
    // Create and populate RTP header
    RTPHeader header;
    header.V = 2;   // RTP version 2
    header.P = 0;   // No padding
    header.X = 0;   // No extension
    header.CC = 0;  // No CSRC
    header.M = is_last_packet ? 1 : 0;  // Marker bit (set for last packet)
    header.PT = 0;  // Payload type 0
    
    // Assign sequence number and SSRC
    assign_sequence_number(&header);
    assign_ssrc(&header);
    
    // Calculate timestamp based on elapsed time and RTP clock rate
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    long elapsed_us = (current_time.tv_sec - start_time.tv_sec) * 1000000 +
                      (current_time.tv_usec - start_time.tv_usec);
    uint32_t timestamp = base_timestamp + (uint32_t)((elapsed_us * RTP_CLOCK_RATE) / 1000000);
    assign_timestamp(&header, timestamp);
    printf("Sequence Number: %d, Timestamp: %u\n", header.seq, timestamp);
    // Build packet
    unsigned char packet[RTP_HEADER_SIZE + payload_size];
    build_rtp_packet(&header, payload, payload_size, packet);
    
    // Send packet
    int packet_size = RTP_HEADER_SIZE + payload_size;
    int bytes_sent = sendto(sockfd, packet, packet_size, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    if (bytes_sent < 0) {
        perror("Failed to send RTP packet");
        return -1;
    }
    return bytes_sent;
}

int send_rtp_packet_with_timestamp(
    int sockfd,
    struct sockaddr_in *server_addr,
    unsigned char *payload,
    int payload_size,
    uint32_t timestamp,
    int is_last_packet) {
    // Build header
    RTPHeader header;
    header.V = 2;
    header.P = 0;
    header.X = 0;
    header.CC = 0;
    header.M = is_last_packet ? 1 : 0;
    header.PT = 96;  // Dynamic PT for video

    assign_sequence_number(&header);
    assign_ssrc(&header);
    assign_timestamp(&header, timestamp);

    unsigned char packet[RTP_HEADER_SIZE + payload_size];
    build_rtp_packet(&header, payload, payload_size, packet);

    int packet_size = RTP_HEADER_SIZE + payload_size;
    return sendto(sockfd, packet, packet_size, 0,
                  (struct sockaddr *)server_addr, sizeof(*server_addr));
}

// High-level function to receive an RTP packet
int receive_rtp_packet(int sockfd, unsigned char *payload, int *payload_size, int *is_last_packet, struct sockaddr_in *client_addr) {
    // Receive raw packet
    unsigned char packet[RTP_HEADER_SIZE + CHUNK_SIZE];
    socklen_t addr_len = sizeof(*client_addr);
    int bytes_received = recvfrom(sockfd, packet, RTP_HEADER_SIZE + CHUNK_SIZE, 0, (struct sockaddr *)client_addr, &addr_len);
    if (bytes_received < 0) {
        perror("Failed to receive RTP packet");
        return -1;
    }
    
    // Unpack RTP header
    RTPHeader header;
    unpack_rtp_header(packet, &header);
    
    // Extract payload
    *payload_size = bytes_received - RTP_HEADER_SIZE;
    memcpy(payload, packet + RTP_HEADER_SIZE, *payload_size);
    
    // Extract marker bit (is_last_packet)
    *is_last_packet = header.M;
    
    return bytes_received;
}

// int main() {
//     return 0;
// }