# RTP Video Streaming System

A real-time video streaming system that demonstrates how video content (MP4 files) can be transmitted over unreliable networks using the Real-time Transport Protocol (RTP). This implementation use RTP features to implement jitter buffering and sequence-base reordering. It also tracks statistics and can provide real-time playback.

## Overview

### What This Project Does

This system takes an MP4 video file and streams it over a network from a **sender** to a **receiver** using the RTP protocol. The video is:
1. Chunked into small payloads (1024 bytes each)
2. Wrapped in RTP headers containing timing and sequencing information
3. Transmitted over UDP to the receiver
4. Buffered and reordered 
5. Reconstructed and played back in real-time

Unlike file transfer, this approach allows video playback to begin before the entire file is received, simulating streaming services like YouTube or Zoom.

### Why RTP (Real-time Transport Protocol)?

RTP is designed for real-time media streaming and operates on top of UDP rather than TCP. This choice is crucial because:

#### UDP vs TCP for Streaming:
- **TCP** provides reliability through retransmission, but this introduces unpredictable delays that are unacceptable for real-time media
- **UDP** delivers packets quickly without waiting for acknowledgments, but offers no guarantees about delivery or ordering
- **RTP** sits on top of UDP and adds the structure needed for media streaming without the delay penalties of TCP

#### Features of RTP:
- **Sequence numbers**: Track packet order and detect loss
- **Timestamps**: Synchronize playback and measure jitter
- **Payload type**: Identify codec and media format (Not fully Implemented)
- **SSRC identifiers**: Distinguish multiple streams (Not Implemented)
- **Marker bits**: Signal frame boundaries

This allows receivers to handle packet loss better while maintaining timing precision.

### How the Sender Works

The **sender** (`sender.c`) performs the following steps:

1. **Load MP4 File**: Reads the video file into memory
2. **Chunk Payload**: Splits the file into 1024-byte chunks (small enough to fit in network packets)
3. **Frame Grouping**: Groups chunks into logical "frames" (e.g., 10 packets per frame at 5 FPS)
4. **RTP Header Creation**: For each chunk, constructs a 12-byte RTP header containing:
   - **Version** (V=2): RTP protocol version
   - **Sequence Number**: Incrementing counter starting from a random value
   - **Timestamp**: Frame-based timestamp using 90kHz clock (RTP standard for video)
   - **SSRC**: Random synchronization source identifier
   - **Marker Bit** (M): Set to 1 on the last packet of each frame
5. **Packet Construction**: Combines RTP header + payload into a complete packet
6. **Timed Transmission**: Sends packets over UDP with precise timing to simulate real-time streaming (e.g., 5 FPS = 200ms per frame)

**Example packet structure:**
```
┌──────────────┬───────────────────────┐
│ RTP Header   │ Payload (1024 bytes)  │
│ (12 bytes)   │ (video data chunk)    │
└──────────────┴───────────────────────┘
```

### How the Receiver Works

The **receiver** (`receiver.c`) must handle the challenges of unreliable network delivery:

1. **Packet Reception**: Listens on UDP port 5000 for incoming packets
2. **RTP Header Parsing**: Extracts critical information from each 12-byte header:
   - **Sequence number**: To detect loss and reordering
   - **Timestamp**: To measure jitter and synchronize playback
   - **Marker bit**: To identify frame boundaries
3. **Jitter Buffer Insertion**: Stores packets in a circular buffer indexed by sequence number
4. **Loss Detection**: Identifies missing sequence numbers (gaps in the sequence)
5. **Reordering Handling**: Detects when packets arrive out-of-order and holds them
6. **Playback Timing**: Waits for a playout delay before releasing packets to smooth out jitter
7. **Reconstruction**: Outputs packets in correct sequence order, skipping permanently lost packets
8. **Real-time Playback**: Streams reconstructed video to FFplay or saves to file

### The Jitter Buffer: Core Concept

The **jitter buffer** help with the problem of variable network delay.

#### The Problem:
```
Sender timing:  Packet sent every 20ms
                ↓     ↓     ↓     ↓     ↓
Network:        ━━━▓▓━━━━▓━━━━━▓▓━━━  (variable delay)
                ↓   ↓ ↓    ↓     ↓ ↓
Receiver:       Packets arrive at irregular intervals (jitter)
```

Without buffering, playback would freeze and be processed out-of-order.

The jitter buffer:
1. **Holds packets** for a fixed delay (e.g., 200ms) before releasing them
2. **Reorders packets** by sequence number during the delay window
3. **Absorbs jitter** by providing a cushion of time for late packets to arrive
4. **Adapts behavior** based on buffer occupancy (drain faster if filling up)

#### Operation:
```
Network delivery:     ━━▓━▓▓━━━▓━━▓━━  (jittery arrival)
                          ↓
Jitter Buffer:        [Wait 200ms] → Reorder → Release
                          ↓
Playback:             ━━━━━━━━━━━━━━  (smooth, sequential)
```

**Adaptive behavior:**
- **Low occupancy (<50%)**: Wait full 200ms for late packets
- **Medium occupancy (50-80%)**: Reduce delay to 100ms to prevent overflow
- **High occupancy (>80%)**: Aggressive drain at 50ms to avoid packet drops

#### Statistics Tracked:
- **Jitter**: Variation in packet inter-arrival time (RFC 3550 calculation)
- **Loss rate**: Percentage of packets that never arrived
- **Reordering**: Packets that arrived after their successors
- **Buffer occupancy**: Current fullness of the jitter buffer

This allows the system to handle network conditions like congestion, variable routing delays, and packet loss while maintaining smooth video playback.

## Architecture

```
┌──────────┐          ┌─────────────────────────┐         ┌──────────────┐
│  Sender  │   RTP    │  Receiver (C)           │  pipe   │ Python GUI   │
│  (C)     │  ─────>  │  - Jitter buffer        │ ─────>  │ + FFplay     │
│          │   UDP    │  - Loss detection       │  video  │              │
└──────────┘          │  - Packet reordering    │         └──────────────┘
                      │  - Statistics tracking  │
                      └─────────────────────────┘
```

## Features

### Sender (`sender.c`)
- **Frame-based timing**: Simulates 30 FPS video streaming
- **RTP timestamps**: Proper RTP clock (90 kHz) for video
- **Configurable rate**: Adjust FPS and packets-per-frame
- **Real-time pacing**: Maintains consistent frame rate

### Receiver (`receiver.c`)
- **Jitter buffer**: 100-packet buffer with 50ms delay
- **Packet reordering**: Handles out-of-order delivery
- **Loss detection**: Tracks missing sequence numbers
- **Statistics**: Reports packet loss, reordering, duplicates
- **Dual output modes**:
  - File mode: Saves to `reconstructed_vid.mp4`
  - Stdout mode: Pipes to GUI for real-time playback

### RTP Library (`rtp.c`, `rtpheaders.c`)
- **High-level API**: Application-layer abstraction
- **Header management**: Auto-generates sequence numbers, timestamps, SSRC
- **Timestamp calculation**: Real-time based RTP timestamps

## Building

```bash
make
```

Builds:
- `sender` - Video streaming sender
- `receiver` - RTP receiver with jitter buffer

## Usage

### Option 1: File Output (Testing)

```bash
# Terminal 1: Start receiver (file mode)
./receiver

# Terminal 2: Send video
./sender samplevid1.mp4

# Play reconstructed video
# Note that this does not work over an unstable network as the reconstructed video may be corrupted. 
ffplay reconstructed_vid.mp4
```

### Option 2: Real-time GUI Playback

```bash
# Terminal 1: Start GUI receiver
python3 receiver_gui.py

# Terminal 2: Send video
./sender samplevid1.mp4
```

The video will display in real-time as it's being received.

## Configuration

### Adjust Streaming Rate

Edit `sender.c`:
```c
#define VIDEO_FPS 30           // Higher = faster transmission
#define PACKETS_PER_FRAME 10   // More packets per frame
```

### Adjust Jitter Buffer

Edit `receiver.c`:
```c
#define JITTER_BUFFER_SIZE 100  // Buffer capacity
#define JITTER_DELAY_MS 50      // Playback delay
```

## Statistics

The receiver reports:
- **Total packets received**
- **Lost packets** (detected via sequence gaps)
- **Reordered packets** (arrived out of order)
- **Duplicate packets**
- **Packet loss rate** (%)

Example output:
```
=== RTP Statistics ===
Total packets received: 1024
Lost packets: 5
Reordered packets: 12
Duplicate packets: 0
Packet loss rate: 0.49%
Total bytes received: 1048576
```

## RTP Header Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           synchronization source (SSRC) identifier            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **V**: Version (2)
- **M**: Marker bit (1 for last packet)
- **PT**: Payload type (0)
- **Sequence**: 16-bit packet counter
- **Timestamp**: 32-bit RTP timestamp (90 kHz clock)
- **SSRC**: 32-bit source identifier

## Requirements

- **C Compiler**: GCC
- **FFmpeg**: For FFplay 
- **Python 3**: For GUI script

## Files

```
sender.c          - Video sender with frame-based timing
receiver.c        - RTP receiver with jitter buffer
receiver_gui.py   - Python GUI wrapper for real-time display
rtp.c             - High-level RTP API
rtpheaders.c      - RTP header packing/unpacking
rtp.h             - RTP header definitions
Makefile          - Build configuration
```

## Testing Network Conditions

### Mininet Testing Setup

This system was tested using **Mininet** to simulate realistic network conditions between two hosts (`h1` and `h2`). The testing topology consisted of:

```
h1 (sender) <──> s1 (switch) <──> h2 (receiver)
                    ↓
            Network impairments:
            - 20ms base delay
            - ±5ms jitter
            - 10% packet loss
            - 10 Mbps bandwidth limit
```

**Test Configuration:**
- **Sender (h1)**: Running `./sender samplevid1.mp4` sending 1024 packets
- **Receiver (h2)**: Running `./receiver` with jitter buffering enabled
- **Network**: 20ms delay, 5ms jitter, 10% loss rate

### Example Test Results

**Network Conditions: 20ms delay, ±5ms jitter, 10% packet loss**

```
=== RTP Statistics ===
Total packets received: 921
Lost packets: 103
Reordered packets: 8
Duplicate packets: 0
Packet loss rate: 10.06%
Total bytes received: 943104

=== Jitter Buffer Statistics ===
Maximum jitter observed: 8.34 ms
Average jitter: 2.67 ms
Jitter measurements: 921
Final buffer occupancy: 0 packets
```

**Analysis:**
- **Packet loss**: 10.06% matches expected 10% network loss rate
- **Reordering**: 8 packets arrived out-of-order due to jitter
- **Jitter**: Max 8.34ms within expected range (±5ms jitter on 20ms delay)
- **Buffer**: Successfully drained to 0 after stream completion
- **Adaptive behavior**: Buffer dynamically adjusted delay to handle congestion

**Network Conditions: Localhost (no impairments)**

```
=== RTP Statistics ===
Total packets received: 1024
Lost packets: 0
Reordered packets: 0
Duplicate packets: 0
Packet loss rate: 0.00%
Total bytes received: 1048576

=== Jitter Buffer Statistics ===
Maximum jitter observed: 0.42 ms
Average jitter: 0.18 ms
Jitter measurements: 1024
Final buffer occupancy: 0 packets
```

**Analysis:**
- **Perfect delivery**: All 1024 packets received in order
- **Low jitter**: <0.5ms typical for localhost
- **No reordering**: Sequential delivery over loopback interface

### Manual Testing (Optional)

For testing without Mininet, you can simulate network conditions using `tc`:

```bash
# Simulate packet loss
sudo tc qdisc add dev lo root netem loss 5%

# Simulate delay and jitter
sudo tc qdisc add dev lo root netem delay 100ms 50ms

# Remove network impairments
sudo tc qdisc del dev lo root
```

The jitter buffer will handle reordering and compensate for variable delays.

## Troubleshooting

**Video doesn't play:**
- Check if sender/receiver are on same port (5000)
- Ensure video file is valid MP4
- Try file mode first to verify data integrity

**High packet loss:**
- Increase jitter buffer size (`JITTER_BUFFER_SIZE` in receiver.c)
- Adjust delay threshold (`JITTER_DELAY_MS`)
- Check network conditions with Mininet or tc stats

**Buffer overflow:**
- Reduce sender rate (decrease `VIDEO_FPS`)
- Increase `JITTER_BUFFER_SIZE`
- Check adaptive delay is working (look for "[JITTER] High buffer occupancy" messages)


All Contributions were done by Laurence Liu :0


Project for CSC458.

