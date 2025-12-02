# RTP Video Streaming System

A real-time video streaming system using RTP (Real-time Transport Protocol) with jitter buffering, packet loss detection, and GUI playback.

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
ffplay reconstructed_vid.mp4
```

### Option 2: Real-time GUI Playback (Recommended)

```bash
# Terminal 1: Start GUI receiver
python3 receiver_gui.py

# Terminal 2: Send video
./sender samplevid1.mp4
```

The video will display in real-time as it's being received.

### Option 3: Manual Pipeline

```bash
# Pipe receiver directly to FFplay
./receiver --stdout | ffplay -i pipe:0 -autoexit
```

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

- **C Compiler**: GCC or Clang
- **FFmpeg**: For FFplay (`brew install ffmpeg` on macOS)
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

### Simulate Packet Loss

```bash
# macOS/Linux: Use tc or pfctl to add loss
sudo tc qdisc add dev lo root netem loss 5%
```

### Simulate Delay/Jitter

```bash
sudo tc qdisc add dev lo root netem delay 100ms 50ms
```

The jitter buffer will handle reordering and compensate for variable delays.

## Troubleshooting

**FFplay not found:**
```bash
brew install ffmpeg  # macOS
apt install ffmpeg   # Linux
```

**Video doesn't play:**
- Check if sender/receiver are on same port (5000)
- Ensure video file is valid MP4
- Try file mode first to verify data integrity

**High packet loss:**
- Increase jitter buffer size
- Adjust delay threshold
- Check network conditions

## Future Enhancements

- [ ] Forward Error Correction (FEC)
- [ ] Selective retransmission (NACK)
- [ ] Adaptive bitrate streaming
- [ ] Multiple receiver support
- [ ] Encryption (SRTP)

## License

Educational project for CSC458.

