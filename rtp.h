#ifndef RTP_H
#define RTP_H

#include <stdint.h>

#define RTP_HEADER_SIZE 12  // Fixed RTP header size (in bytes)
#define CHUNK_SIZE 1024     // Max payload size for each packet

// RTP Header Structure
typedef struct {
    uint8_t V:2;          // Version (2 bits)
    uint8_t P:1;          // Padding (1 bit)
    uint8_t X:1;          // Extension (1 bit)
    uint8_t CC:4;         // CSRC count (4 bits)
    uint8_t M:1;          // Marker (1 bit)
    uint8_t PT:7;         // Payload Type (7 bits)
    uint16_t seq;         // Sequence number (16 bits)
    uint32_t timestamp;   // Timestamp (32 bits)
    uint32_t ssrc;        // SSRC (Synchronization Source) (32 bits)
} RTPHeader;

// High-level API (Application Layer)
int send_rtp_packet(int sockfd, struct sockaddr_in *server_addr, unsigned char *payload, int payload_size, int is_last_packet);
int receive_rtp_packet(int sockfd, unsigned char *payload, int *payload_size, int *is_last_packet, struct sockaddr_in *client_addr);
//int send_rtp_packet_with_timestamp(int sockfd, struct sockaddr_in *server_addr, unsigned char *payload, int payload_size, uint32_t timestamp, int is_last_packet);
int send_rtp_packet_with_timestamp(int sockfd,
    struct sockaddr_in *server_addr,
    unsigned char *payload,
    int payload_size,
    uint32_t timestamp,
    int is_last_packet);
// Low-level API (Internal/Library use)
void build_rtp_packet(RTPHeader *header, unsigned char *payload, int payload_size, unsigned char *packet);
void unpack_rtp_header(unsigned char *packet, RTPHeader *header);
void assign_sequence_number(RTPHeader *header);
void assign_timestamp(RTPHeader *header, uint32_t timestamp);
void assign_ssrc(RTPHeader *header);

#endif // RTP_H
