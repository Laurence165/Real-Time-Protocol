#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include "rtp.h"
#include "rtp.c"

// Video streaming parameters
#define VIDEO_FPS 5
#define FRAME_DURATION_US (1000000 / VIDEO_FPS)  // ~33333 microseconds per frame
#define PACKETS_PER_FRAME 10  // Simulate 10 packets per video frame
#define RTP_CLOCK_RATE 90000  // Standard RTP clock rate for video (90 kHz)

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
    
    // Random initial RTP timestamp (RTP best practice)
    srand(time(NULL));
    uint32_t base_timestamp = (uint32_t)(rand() & 0xFFFFFFFF);
    
    printf("Initial RTP timestamp: %u\n", base_timestamp);
    printf("RTP clock rate: 90000 Hz (standard for video)\n");
    printf("Timestamp increment per frame: %d (90000/%d FPS)\n\n", 90000/VIDEO_FPS, VIDEO_FPS);
    
    for (int i = 0; i < num_chunks; i++) {
        int current_frame = i / PACKETS_PER_FRAME;
    
        // Correct timestamp
        uint32_t frame_timestamp =
            base_timestamp + current_frame * (RTP_CLOCK_RATE / VIDEO_FPS);
    
        int offset = i * CHUNK_SIZE;
        int payload_size = (i == num_chunks - 1)
                            ? (file_size - offset)
                            : CHUNK_SIZE;
    
        // Marker bit = last packet of *frame*, not whole file
        int is_last_in_frame = ((i + 1) % PACKETS_PER_FRAME == 0);
    
        int bytes_sent = send_rtp_packet_with_timestamp(
            sockfd, &server_addr,
            buffer + offset,
            payload_size,
            frame_timestamp,
            is_last_in_frame
        );
        
        if (bytes_sent < 0) {
            fprintf(stderr, "Failed to send packet %d\n", i);
            break;
        }
    
        printf("Sent pkt %d (frame=%d, ts=%u, M=%d, %d bytes)\n",
               i, current_frame, frame_timestamp, is_last_in_frame, bytes_sent);
    
        // Spread packets evenly within the frame
        usleep(FRAME_DURATION_US / PACKETS_PER_FRAME);
    
        // After last packet of the frame, ensure we haven't fallen behind
        if (is_last_in_frame) {
            gettimeofday(&current_time, NULL);
            long elapsed = (current_time.tv_sec - start_time.tv_sec) * 1000000 +
                           (current_time.tv_usec - start_time.tv_usec);
    
            long expected = (long)(current_frame + 1) * FRAME_DURATION_US;
    
            if (elapsed < expected)
                usleep(expected - elapsed);
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
