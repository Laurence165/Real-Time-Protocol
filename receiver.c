#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include "rtp.h"
#include "rtp.c"

#define JITTER_BUFFER_SIZE 5000  // Hold up to 5000 packets in buffer
#define JITTER_DELAY_MS 200      // Wait 100ms before playing out (handles reordering and jitter)
#define MAX_JITTER_MS 200        // Maximum jitter tolerance
#define MISSING_PACKET_TIMEOUT_MS 50

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
    int buffer_count;  // Number of filled slots
    long max_jitter_us;  // Maximum observed jitter (microseconds)
    long avg_jitter_us;  // Average jitter (RFC 3550 calculation)
    int jitter_samples;  // Number of jitter measurements
    struct timeval last_arrival_time;  // Time of last packet arrival
    uint32_t last_timestamp;  // RTP timestamp of last packet
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
    
    // Set socket timeout for end-of-stream detection
    struct timeval timeout;
    timeout.tv_sec = 5;   // 5 seconds timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    fprintf(stderr, "Stream timeout set to 5 seconds\n");
    fflush(stderr);
    
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
        
        // Check for timeout (end of stream)
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                fprintf(stderr, "[STREAM] No packets received for 5 seconds - stream ended\n");
                fflush(stderr);
                stream_ended = 1;
                break;
            } else {
                perror("recvfrom error");
                break;
            }
        }
        
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
        
        // M=1 marks end of FRAME, not end of stream
        if (is_last_packet) {
            fprintf(stderr, "[FRAME] End of frame marker (M=1) at seq=%u, ts=%u\n", header.seq, header.timestamp);
            fflush(stderr);
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
    
    // Drain remaining packets from jitter buffer
    fprintf(stderr, "[STREAM] Draining jitter buffer (skipping missing packets)...\n");
    fflush(stderr);
    
    unsigned char drain_payload[CHUNK_SIZE];
    int drain_size, drain_last;
    int drained_count = 0;
    int skipped_count = 0;
    int max_drain_attempts = JITTER_BUFFER_SIZE;  // Prevent infinite loop
    
    for (int attempt = 0; attempt < max_drain_attempts && jb.buffer_count > 0; attempt++) {
        uint16_t expected_seq = (jb.base_seq + jb.head) & 0xFFFF;
        int buffer_idx = jb.head % JITTER_BUFFER_SIZE;
        
        if (jb.entries[buffer_idx].filled) {
            // Packet available - drain it
            if (get_from_jitter_buffer(&jb, drain_payload, &drain_size, &drain_last, 1)) {
                drained_count++;
                if (output_to_stdout) {
                    fwrite(drain_payload, 1, drain_size, output_file);
                    fflush(output_file);
                } else {
                    memcpy(reconstructed_video + total_bytes, drain_payload, drain_size);
                }
                total_bytes += drain_size;
            }
        } else {
            // Missing packet - skip it
            fprintf(stderr, "[DRAIN] Skipping missing seq=%u\n", expected_seq);
            fflush(stderr);
            jb.head++;  // Move past missing packet
            skipped_count++;
        }
    }
    
    fprintf(stderr, "[STREAM] Drained %d packets, skipped %d missing, final occupancy: %d\n", 
            drained_count, skipped_count, jb.buffer_count);
    fflush(stderr);

    // Write to file if not stdout mode
    if (!output_to_stdout) {
        FILE *out = fopen("reconstructed_vid.mp4", "wb");
        fwrite(reconstructed_video, 1, total_bytes, out);
        fclose(out);
        fprintf(stderr, "\nVideo saved to reconstructed_vid.mp4\n");
    }

    fprintf(stderr, "\n=== RTP Statistics ===\n");
    print_statistics(&stats);
    fprintf(stderr, "Total bytes received: %d\n", total_bytes);
    fprintf(stderr, "\n=== Jitter Buffer Statistics ===\n");
    fprintf(stderr, "Maximum jitter observed: %.2f ms\n", jb.max_jitter_us / 1000.0);
    fprintf(stderr, "Average jitter: %.2f ms\n", jb.avg_jitter_us / 1000.0);
    fprintf(stderr, "Jitter measurements: %d\n", jb.jitter_samples);
    fprintf(stderr, "Final buffer occupancy: %d packets\n", jb.buffer_count);
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
    jb->buffer_count = 0;
    jb->max_jitter_us = 0;
    jb->avg_jitter_us = 0;
    jb->jitter_samples = 0;
    jb->last_timestamp = 0;
    memset(&jb->last_arrival_time, 0, sizeof(struct timeval));
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
    
    // Detect loss and reordering
    if (stats->first_packet) {
        stats->first_packet = 0;
    } else {
        uint16_t expected_seq = stats->last_seq + 1;
        if (seq != expected_seq) {
            int16_t gap = (int16_t)(seq - expected_seq);  // Signed for negative detection
            
            if (gap > 0 && gap < 100) {
                // Forward jump (potential loss or out-of-order arrival)
                // Don't immediately count as loss - jitter buffer will handle it
                fprintf(stderr, "[SEQ] Forward jump: expected seq=%u, got seq=%u (gap=%d)\n", 
                       expected_seq, seq, gap);
                fflush(stderr);
                
                // Mark as potential loss (jitter buffer will correct if packets arrive late)
                stats->lost_packets += gap;
                
            } else if (gap < 0) {
                // Backward jump - packet arrived LATE (reordering!)
                stats->reordered_packets++;
                stats->lost_packets--;  // Correct the false loss detection
                fprintf(stderr, "[REORDER] seq=%u arrived late (expected %u)\n", seq, expected_seq);
                fflush(stderr);
                
            } else if (gap >= 100) {
                // Huge forward gap - definitely loss or wraparound
                if (gap < 30000) {  // Not wraparound
                    stats->lost_packets += gap;
                    fprintf(stderr, "[LOSS] Large gap: expected seq=%u, got seq=%u (lost %d)\n", 
                           expected_seq, seq, gap);
                    fflush(stderr);
                } else {
                    // Likely sequence number wraparound or very late packet
                    stats->reordered_packets++;
                    fprintf(stderr, "[REORDER] seq=%u arrived very late (wraparound?)\n", seq);
                    fflush(stderr);
                }
            }
        }
    }
    
    // Update last seen sequence (only if newer)
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
    
    // Calculate actual network jitter (RFC 3550 algorithm)
    struct timeval arrival_time;
    gettimeofday(&arrival_time, NULL);
    
    if (jb->jitter_samples > 0) {
        // Calculate inter-arrival time (in microseconds)
        long arrival_diff_us = (arrival_time.tv_sec - jb->last_arrival_time.tv_sec) * 1000000 +
                               (arrival_time.tv_usec - jb->last_arrival_time.tv_usec);
        
        // Calculate RTP timestamp difference (convert to microseconds, RTP clock = 90kHz)
        long timestamp_diff = ((long)timestamp - (long)jb->last_timestamp);
        long timestamp_diff_us = (timestamp_diff * 1000000) / 90000;
        
        // Jitter = variance in inter-arrival times
        long jitter_us = arrival_diff_us - timestamp_diff_us;
        if (jitter_us < 0) jitter_us = -jitter_us;  // Absolute value
        
        // Update max jitter
        if (jitter_us > jb->max_jitter_us) {
            jb->max_jitter_us = jitter_us;
        }
        
        // RFC 3550 jitter calculation: J = J + (|D| - J) / 16
        jb->avg_jitter_us = jb->avg_jitter_us + (jitter_us - jb->avg_jitter_us) / 16;
        
        fprintf(stderr, "[JITTER] Packet jitter: %.2f ms (max=%.2f ms, avg=%.2f ms)\n",
                jitter_us / 1000.0, jb->max_jitter_us / 1000.0, jb->avg_jitter_us / 1000.0);
        fflush(stderr);
    }
    
    // Update last arrival info
    jb->last_arrival_time = arrival_time;
    jb->last_timestamp = timestamp;
    jb->jitter_samples++;
    
    // Store in buffer
    if (!jb->entries[buffer_idx].filled) {
        jb->buffer_count++;  // Increment count for new entry
    }
    
    memcpy(jb->entries[buffer_idx].payload, payload, payload_size);
    jb->entries[buffer_idx].payload_size = payload_size;
    jb->entries[buffer_idx].seq_number = seq;
    jb->entries[buffer_idx].timestamp = timestamp;
    jb->entries[buffer_idx].is_last_packet = is_last;
    jb->entries[buffer_idx].filled = 1;
    jb->entries[buffer_idx].arrival_time = arrival_time;
    
    fprintf(stderr, "[BUFFER] Occupancy: %d/%d (%.1f%%)\n", 
            jb->buffer_count, JITTER_BUFFER_SIZE, 
            (float)jb->buffer_count / JITTER_BUFFER_SIZE * 100.0);
    fflush(stderr);
    
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
        // --- Missing packet timeout logic ---
        struct timeval now;
        gettimeofday(&now, NULL);
    
        long waited_us =
            (now.tv_sec - jb->last_arrival_time.tv_sec) * 1000000 +
            (now.tv_usec - jb->last_arrival_time.tv_usec);
    
        long waited_ms = waited_us / 1000;
    
        if (waited_ms > MISSING_PACKET_TIMEOUT_MS) {
            fprintf(stderr,
                    "[JB] Missing packet seq=%u timed out after %ld ms → skipping\n",
                    expected_seq, waited_ms);
    
            jb->head++;  // MOVE ON → skip the missing packet
            return 0;
        }
    
        fprintf(stderr,
                "[JB] Slot empty for seq=%u, waited %ldms (< timeout). Holding...\n",
                expected_seq, waited_ms);
    
        return 0;
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
        long elapsed_us = (now.tv_sec - jb->entries[buffer_idx].arrival_time.tv_sec) * 1000000 +
                          (now.tv_usec - jb->entries[buffer_idx].arrival_time.tv_usec);
        long elapsed_ms = elapsed_us / 1000;
        
        
        // Adaptive playout delay based on buffer occupancy
        int adaptive_delay_ms = JITTER_DELAY_MS;
        float buffer_fill_ratio = (float)jb->buffer_count / JITTER_BUFFER_SIZE;
        
        if (buffer_fill_ratio > 0.8) {
            // Buffer filling up - drain faster to avoid overflow
            adaptive_delay_ms = JITTER_DELAY_MS / 4;
            fprintf(stderr, "[JITTER] High buffer occupancy (%.1f%%) - reducing delay to %dms\n",
                    buffer_fill_ratio * 100.0, adaptive_delay_ms);
            fflush(stderr);
        } else if (buffer_fill_ratio > 0.5) {
            // Buffer moderately full - slightly reduce delay
            adaptive_delay_ms = JITTER_DELAY_MS / 2;
        }
        
        fprintf(stderr, "[DEBUG JB] Playout delay check: elapsed=%ldms, threshold=%dms, is_last=%d, buffer=%.1f%%\n", 
                elapsed_ms, adaptive_delay_ms, jb->entries[buffer_idx].is_last_packet, buffer_fill_ratio * 100.0);
        fflush(stderr);
        
        if (elapsed_ms < adaptive_delay_ms && !jb->entries[buffer_idx].is_last_packet) {
            fprintf(stderr, "[DEBUG JB] Waiting for jitter delay (%ld/%d ms)\n", elapsed_ms, adaptive_delay_ms);
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
    jb->buffer_count--;  // Decrement count
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
