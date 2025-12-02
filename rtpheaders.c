#include "rtp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>




// Function to assign the timestamp
void assign_sequence_number(RTPHeader *header) {
    //random initial sequence number then increment by 1 for each packet
    static uint16_t seq = 0;  // Static variable to persist sequence number across function calls
    static int initialized = 0;
    if (!initialized) {
        srand(time(NULL));
        seq = (uint16_t)(rand() & 0xFFFF);  // Random initial sequence number
        initialized = 1;
    }
    header->seq = seq++;
}

void assign_timestamp(RTPHeader *header, uint32_t timestamp) {
    header->timestamp = timestamp;
}

void assign_ssrc(RTPHeader *header) {
    header->ssrc = (uint32_t)rand() << 16 | (uint32_t)rand();
}

void build_rtp_packet(RTPHeader *header, unsigned char *payload, int payload_size, unsigned char *packet) {
    // Manually pack RTP header fields into bytes (bitfield layout is compiler-dependent)
    packet[0] = (header->V << 6) | (header->P << 5) | (header->X << 4) | header->CC;
    packet[1] = (header->M << 7) | header->PT;
    packet[2] = (header->seq >> 8) & 0xFF;  // Sequence number high byte
    packet[3] = header->seq & 0xFF;  // Sequence number low byte
    packet[4] = (header->timestamp >> 24) & 0xFF;  // Timestamp bytes
    packet[5] = (header->timestamp >> 16) & 0xFF;
    packet[6] = (header->timestamp >> 8) & 0xFF;
    packet[7] = header->timestamp & 0xFF;
    packet[8] = (header->ssrc >> 24) & 0xFF;  // SSRC bytes
    packet[9] = (header->ssrc >> 16) & 0xFF;
    packet[10] = (header->ssrc >> 8) & 0xFF;
    packet[11] = header->ssrc & 0xFF;

    // Copy payload data after header (starting from index 12)
    memcpy(packet + RTP_HEADER_SIZE, payload, payload_size);
}

void unpack_rtp_header(unsigned char *packet, RTPHeader *header) {
    header->V = (packet[0] >> 6) & 0x03;
    header->P = (packet[0] >> 5) & 0x01;
    header->X = (packet[0] >> 4) & 0x01;
    header->CC = packet[0] & 0x0F;
    header->M = (packet[1] >> 7) & 0x01;
    header->PT = packet[1] & 0x7F;
    header->seq = (packet[2] << 8) | packet[3];
    header->timestamp = (packet[4] << 24) | (packet[5] << 16) | (packet[6] << 8) | packet[7];
    header->ssrc = (packet[8] << 24) | (packet[9] << 16) | (packet[10] << 8) | packet[11];
}