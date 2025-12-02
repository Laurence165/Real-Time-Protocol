#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "rtp.h"
#include "rtp.c"

#define JITTER_BUFFER_SIZE 5000  // Hold up to 5000 packets in buffer
#define JITTER_DELAY_MS 0        // Wait 0ms on localhost (increase for real networks)

// Jitter buffer entry
typedef struct {
    unsigned char payload[CHUNK_SIZE];
    int payload_size;
    uint16_t seq_number;
    uint32_t timestamp;
    int is_last_packet;
    int filled;
    struct timeval arrival_time;
} BufferEntry;

// Statistics tracking
typedef struct {
    int total_packets;
    int lost_packets;
    int reordered_packets;
    int duplicate_packets;
    uint16_t last_seq;
    int first_packet;
} RTPStats;

// Jitter buffer
typedef struct {
    BufferEntry entries[JITTER_BUFFER_SIZE];
    int head;  // Next sequence to play out
    int tail;  // Last received sequence
    uint16_t base_seq;  // First sequence number received
    int initialized;
} JitterBuffer;

// Function prototypes
void init_jitter_buffer(JitterBuffer *jb);
int add_to_jitter_buffer(JitterBuffer *jb, unsigned char *payload, int payload_size, 
                         uint16_t seq, uint32_t timestamp, int is_last, RTPStats *stats);
int get_from_jitter_buffer(JitterBuffer *jb, unsigned char *payload, int *payload_size, int *is_last, int force_flush);
void print_statistics(RTPStats *stats);

int main(int argc, char *argv[]) {
    int output_to_stdout = 0;
    
    // Parse command line arguments
    if (argc > 1 && strcmp(argv[1], "--stdout") == 0) {
        output_to_stdout = 1;
    }
    
    // Create UDP socket
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    // Initialize jitter buffer and statistics
    JitterBuffer jb;
    RTPStats stats = {0};
    stats.first_packet = 1;
    init_jitter_buffer(&jb);

    // Allocate memory for reconstructed video
    unsigned char *reconstructed_video = (unsigned char *)malloc(10 * 1024 * 1024);  // 10MB buffer
    int total_bytes = 0;

    FILE *output_file = NULL;
    if (output_to_stdout) {
        output_file = stdout;
        // stderr will naturally go to terminal (not piped)
    }

    fprintf(stderr, "RTP Receiver started (port 5000)\n");
    fprintf(stderr, "Output mode: %s\n", output_to_stdout ? "stdout" : "file");
    fprintf(stderr, "Waiting for packets...\n\n");
    fflush(stderr);

    int stream_ended = 0;
    unsigned char recv_payload[CHUNK_SIZE];
    
    // Receive loop
    while (!stream_ended) {
        int payload_size;
        int is_last_packet;
        
        fprintf(stderr, "[DEBUG] Waiting for packet...\n");
        fflush(stderr);
        
        // Receive packet (this internally handles RTP header parsing)
        unsigned char packet[RTP_HEADER_SIZE + CHUNK_SIZE];
        socklen_t addr_len = sizeof(client_addr);
        int n = recvfrom(sockfd, packet, RTP_HEADER_SIZE + CHUNK_SIZE, 0, 
                        (struct sockaddr *)&client_addr, &addr_len);
        
        fprintf(stderr, "[DEBUG] Received %d bytes\n", n);
        fflush(stderr);
        
        if (n < RTP_HEADER_SIZE) continue;
        
        // Manually unpack header to get sequence number and timestamp
        RTPHeader header;
        unpack_rtp_header(packet, &header);
        
        payload_size = n - RTP_HEADER_SIZE;
        memcpy(recv_payload, packet + RTP_HEADER_SIZE, payload_size);
        is_last_packet = header.M;
        
        fprintf(stderr, "[DEBUG] Packet seq=%u, M=%d, payload=%d bytes\n", header.seq, header.M, payload_size);
        fflush(stderr);
        
        // Add to jitter buffer
        add_to_jitter_buffer(&jb, recv_payload, payload_size, header.seq, 
                            header.timestamp, is_last_packet, &stats);
        
        // If last packet received (M=1), end stream regardless of buffer state
        if (is_last_packet) {
            fprintf(stderr, "[DEBUG] Last packet received (M=1)! Ending stream.\n");
            fflush(stderr);
            stream_ended = 1;
            
            // Try to drain remaining packets from buffer
            unsigned char ordered_payload[CHUNK_SIZE];
            int ordered_size;
            int ordered_last;
            
            fprintf(stderr, "[DEBUG] Attempting to drain remaining packets...\n");
            fflush(stderr);
            
            int drained = 0;
            while (get_from_jitter_buffer(&jb, ordered_payload, &ordered_size, &ordered_last, 1)) {
                drained++;
                // Write payload to output
                if (output_to_stdout) {
                    fwrite(ordered_payload, 1, ordered_size, output_file);
                    fflush(output_file);
                } else {
                    memcpy(reconstructed_video + total_bytes, ordered_payload, ordered_size);
                }
                total_bytes += ordered_size;
            }
            
            fprintf(stderr, "[DEBUG] Drained %d packets from buffer\n", drained);
            fflush(stderr);
            break;  // Exit receive loop immediately
        }
        
        // Try to retrieve packets from jitter buffer in order (normal operation)
        unsigned char ordered_payload[CHUNK_SIZE];
        int ordered_size;
        int ordered_last;
        
        int packets_retrieved = 0;
        while (get_from_jitter_buffer(&jb, ordered_payload, &ordered_size, &ordered_last, 0)) {
            packets_retrieved++;
            fprintf(stderr, "[DEBUG] Retrieved packet from buffer (count=%d, last=%d)\n", packets_retrieved, ordered_last);
            fflush(stderr);
            
            // Write payload to output
            if (output_to_stdout) {
                fwrite(ordered_payload, 1, ordered_size, output_file);
                fflush(output_file);
            } else {
                memcpy(reconstructed_video + total_bytes, ordered_payload, ordered_size);
            }
            
            total_bytes += ordered_size;
        }
        
        fprintf(stderr, "[DEBUG] Retrieved %d packets this iteration. stream_ended=%d\n", packets_retrieved, stream_ended);
        fflush(stderr);
    }
    
    fprintf(stderr, "[DEBUG] Exited receive loop\n");
    fflush(stderr);

    // Write to file if not stdout mode
    if (!output_to_stdout) {
        FILE *out = fopen("reconstructed_vid.txt", "wb");
        fwrite(reconstructed_video, 1, total_bytes, out);
        fclose(out);
        fprintf(stderr, "\nVideo saved to reconstructed_vid.txt\n");
    }

    fprintf(stderr, "\n=== RTP Statistics ===\n");
    print_statistics(&stats);
    fprintf(stderr, "Total bytes received: %d\n", total_bytes);
    fflush(stderr);

    // Clean up
    close(sockfd);
    free(reconstructed_video);

    return 0;
}

void init_jitter_buffer(JitterBuffer *jb) {
    memset(jb, 0, sizeof(JitterBuffer));
    for (int i = 0; i < JITTER_BUFFER_SIZE; i++) {
        jb->entries[i].filled = 0;
    }
    jb->initialized = 0;
}

int add_to_jitter_buffer(JitterBuffer *jb, unsigned char *payload, int payload_size,
                         uint16_t seq, uint32_t timestamp, int is_last, RTPStats *stats) {
    stats->total_packets++;
    
    // Initialize base sequence on first packet
    if (!jb->initialized) {
        jb->base_seq = seq;
        jb->head = 0;
        jb->tail = 0;
        jb->initialized = 1;
        stats->last_seq = seq - 1;
        fprintf(stderr, "First packet: seq=%u\n", seq);
        fflush(stderr);
    }
    
    // Detect loss
    if (stats->first_packet) {
        stats->first_packet = 0;
    } else {
        uint16_t expected_seq = stats->last_seq + 1;
        if (seq != expected_seq) {
            int gap = (seq - expected_seq) & 0xFFFF;
            if (gap > 0 && gap < 1000) {  // Reasonable gap
                stats->lost_packets += gap;
                fprintf(stderr, "Loss detected: expected seq=%u, got seq=%u (lost %d)\n", 
                       expected_seq, seq, gap);
                fflush(stderr);
            } else if (gap > 1000) {
                // Likely reordering
                stats->reordered_packets++;
                fprintf(stderr, "Reordering: seq=%u arrived late\n", seq);
                fflush(stderr);
            }
        }
    }
    
    // Update last seen sequence
    if (((int16_t)(seq - stats->last_seq)) > 0) {
        stats->last_seq = seq;
    }
    
    // Calculate buffer index
    int relative_seq = (seq - jb->base_seq) & 0xFFFF;
    int buffer_idx = relative_seq % JITTER_BUFFER_SIZE;
    
    // Check for duplicate
    if (jb->entries[buffer_idx].filled && jb->entries[buffer_idx].seq_number == seq) {
        stats->duplicate_packets++;
        fprintf(stderr, "Duplicate packet: seq=%u\n", seq);
        fflush(stderr);
        return -1;
    }
    
    // Check for buffer overwrite (slot filled with different sequence)
    if (jb->entries[buffer_idx].filled && jb->entries[buffer_idx].seq_number != seq) {
        uint16_t overwritten_seq = jb->entries[buffer_idx].seq_number;
        fprintf(stderr, "Buffer overflow: seq=%u overwrites seq=%u at idx=%d\n", 
                seq, overwritten_seq, buffer_idx);
        fflush(stderr);
        // This is effectively a lost packet (buffer too small)
        stats->lost_packets++;
    }
    
    // Store in buffer
    memcpy(jb->entries[buffer_idx].payload, payload, payload_size);
    jb->entries[buffer_idx].payload_size = payload_size;
    jb->entries[buffer_idx].seq_number = seq;
    jb->entries[buffer_idx].timestamp = timestamp;
    jb->entries[buffer_idx].is_last_packet = is_last;
    jb->entries[buffer_idx].filled = 1;
    gettimeofday(&jb->entries[buffer_idx].arrival_time, NULL);
    
    return 0;
}

int get_from_jitter_buffer(JitterBuffer *jb, unsigned char *payload, int *payload_size, int *is_last, int force_flush) {
    if (!jb->initialized) {
        fprintf(stderr, "[DEBUG JB] Buffer not initialized\n");
        fflush(stderr);
        return 0;
    }
    
    // Calculate expected sequence number
    uint16_t expected_seq = (jb->base_seq + jb->head) & 0xFFFF;
    int buffer_idx = jb->head % JITTER_BUFFER_SIZE;
    
    fprintf(stderr, "[DEBUG JB] Looking for seq=%u at idx=%d, force_flush=%d\n", expected_seq, buffer_idx, force_flush);
    fflush(stderr);
    
    // Check if packet is available
    if (!jb->entries[buffer_idx].filled) {
        fprintf(stderr, "[DEBUG JB] Slot not filled\n");
        fflush(stderr);
        return 0;  // Packet not yet received
    }
    
    fprintf(stderr, "[DEBUG JB] Found seq=%u in slot (expected %u)\n", jb->entries[buffer_idx].seq_number, expected_seq);
    fflush(stderr);
    
    if (jb->entries[buffer_idx].seq_number != expected_seq) {
        fprintf(stderr, "[DEBUG JB] Sequence mismatch!\n");
        fflush(stderr);
        return 0;  // Wrong packet in slot (shouldn't happen)
    }
    
    // Check jitter delay (wait a bit to allow reordering) unless forced
    if (!force_flush) {
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - jb->entries[buffer_idx].arrival_time.tv_sec) * 1000 +
                          (now.tv_usec - jb->entries[buffer_idx].arrival_time.tv_usec) / 1000;
        
        fprintf(stderr, "[DEBUG JB] Jitter check: elapsed=%ldms, threshold=%d, is_last=%d\n", 
                elapsed_ms, JITTER_DELAY_MS, jb->entries[buffer_idx].is_last_packet);
        fflush(stderr);
        
        if (elapsed_ms < JITTER_DELAY_MS && !jb->entries[buffer_idx].is_last_packet) {
            fprintf(stderr, "[DEBUG JB] Waiting for jitter delay\n");
            fflush(stderr);
            return 0;  // Wait longer for potential reordered packets
        }
    }
    
    // Retrieve packet
    memcpy(payload, jb->entries[buffer_idx].payload, jb->entries[buffer_idx].payload_size);
    *payload_size = jb->entries[buffer_idx].payload_size;
    *is_last = jb->entries[buffer_idx].is_last_packet;
    
    fprintf(stderr, "[DEBUG JB] Successfully retrieved seq=%u, size=%d, is_last=%d\n", 
            jb->entries[buffer_idx].seq_number, *payload_size, *is_last);
    fflush(stderr);
    
    // Mark as empty and advance head
    jb->entries[buffer_idx].filled = 0;
    jb->head++;
    
    return 1;
}

void print_statistics(RTPStats *stats) {
    fprintf(stderr, "Total packets received: %d\n", stats->total_packets);
    fprintf(stderr, "Lost packets: %d\n", stats->lost_packets);
    fprintf(stderr, "Reordered packets: %d\n", stats->reordered_packets);
    fprintf(stderr, "Duplicate packets: %d\n", stats->duplicate_packets);
    if (stats->total_packets > 0) {
        float loss_rate = (float)stats->lost_packets / (stats->total_packets + stats->lost_packets) * 100.0;
        fprintf(stderr, "Packet loss rate: %.2f%%\n", loss_rate);
    }
}
