#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "rtp.h"
#include "rtp.c"

// Video streaming parameters
#define VIDEO_FPS 15
#define FRAME_DURATION_US (1000000 / VIDEO_FPS)  // ~33333 microseconds per frame
#define PACKETS_PER_FRAME 10  // Simulate 10 packets per video frame

int main(int argc, char *argv[]) {
    // Open image file for reading
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <video_file> <receiver_ip> \n", argv[0]);
        return 1;
    }
    FILE *image_file = fopen(argv[1], "rb");
    char *ip = argv[2];
    if (!image_file) {
        perror("Unable to open image file");
        return 1;
    }

    // Get file size and allocate memory for image
    fseek(image_file, 0, SEEK_END);
    long file_size = ftell(image_file);
    rewind(image_file);
    unsigned char *buffer = (unsigned char *)malloc(file_size);
    fread(buffer, 1, file_size, image_file);

    // Create UDP socket
    int sockfd;
    struct sockaddr_in server_addr;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Set up server address (receiver)
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);  // Receiver port
    server_addr.sin_addr.s_addr = inet_addr(ip);  // Localhost

    // Calculate number of chunks (for dynamic chunking)
    int num_chunks = (file_size / CHUNK_SIZE) + (file_size % CHUNK_SIZE != 0);  // Handle remainder
    int num_frames = (num_chunks / PACKETS_PER_FRAME) + (num_chunks % PACKETS_PER_FRAME != 0);
    printf("Number of chunks: %d\n", num_chunks);
    printf("Simulating %d video frames at %d FPS (%d packets per frame)\n", num_frames, VIDEO_FPS, PACKETS_PER_FRAME);
    
    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);
    
    // Send video in chunks with frame-based timing
    for (int i = 0; i < num_chunks; i++) {
    //     int current_frame = i / PACKETS_PER_FRAME;
    // uint32_t frame_timestamp = current_frame * (90000 / VIDEO_FPS);  // Frame's timestamp
    
        int offset = i * CHUNK_SIZE;
        int payload_size = (i == num_chunks - 1) ? (file_size - offset) : CHUNK_SIZE;
        
        int is_last_packet = (i == num_chunks - 1);  // Check if this is the last packet
        
        // Send RTP packet (RTP library handles all header logic)
        int bytes_sent = send_rtp_packet(sockfd, &server_addr, buffer + offset, payload_size, is_last_packet);
        if (bytes_sent < 0) {
            fprintf(stderr, "Failed to send packet %d\n", i);
            break;
        }
        
        int current_frame = i / PACKETS_PER_FRAME;
        printf("Sent packet %d/%d (frame %d, %d bytes, last=%d)\n", i, num_chunks, current_frame, bytes_sent, is_last_packet);
        
        // Simulate frame-based timing: delay at frame boundaries
        if ((i + 1) % PACKETS_PER_FRAME == 0 && !is_last_packet) {
            // Calculate how long we should wait to maintain frame rate
            int expected_frame = (i + 1) / PACKETS_PER_FRAME;
            long expected_time_us = expected_frame * FRAME_DURATION_US;
            
            gettimeofday(&current_time, NULL);
            long elapsed_us = (current_time.tv_sec - start_time.tv_sec) * 1000000 +
                             (current_time.tv_usec - start_time.tv_usec);
            //long jitter_delay_us = rand() % 100000;
            long sleep_time_us = expected_time_us - elapsed_us;
            if (sleep_time_us > 0) {
                usleep(sleep_time_us );//+ //jitter_delay_us);
                printf("  [Frame %d complete - waited %ld µs to maintain %d FPS]\n", 
                       current_frame, sleep_time_us, VIDEO_FPS);
            }
        }
    }

    gettimeofday(&current_time, NULL);
    long total_time_ms = ((current_time.tv_sec - start_time.tv_sec) * 1000000 +
                          (current_time.tv_usec - start_time.tv_usec)) / 1000;
    printf("Video transmission completed in %ld ms (%.2f seconds)\n", total_time_ms, total_time_ms / 1000.0);

    // Clean up and close socket
    close(sockfd);
    free(buffer);
    fclose(image_file);

    return 0;
}
