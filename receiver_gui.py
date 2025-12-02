#!/usr/bin/env python3
"""
RTP Video Receiver with GUI Display
Uses C receiver for RTP protocol handling and FFplay for video display
"""
import subprocess
import sys
import os

def main():
    # Check if receiver binary exists
    receiver_path = './receiver'
    if not os.path.exists(receiver_path):
        print("Error: receiver binary not found. Run 'make' first.")
        sys.exit(1)
    
    print("Starting RTP video receiver with GUI...")
    print("Press Ctrl+C to stop\n")
    
    try:
        # Start C receiver in stdout mode (pipes video data)
        receiver = subprocess.Popen(
            [receiver_path, '--stdout'],
            stdout=subprocess.PIPE,
            stderr=None
        )
        
        # Start FFplay to display video from receiver's stdout
        ffplay = subprocess.Popen(
            ['ffplay', '-i', 'pipe:0', '-autoexit', '-framedrop'],
            stdin=receiver.stdout,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        
        # Monitor receiver's stderr for statistics
        # def print_receiver_output():
        #     for line in iter(receiver.stderr.readline, b''):
        #         print(line.decode('utf-8'), end='')
        
        # Wait for processes to complete
        ffplay.wait()
        
        # Wait for receiver to finish and print statistics
        receiver.wait()
        
        print("\nVideo playback completed.")
        
    except KeyboardInterrupt:
        print("\nStopping...")
        try:
            receiver.terminate()
            ffplay.terminate()
        except:
            pass
    except FileNotFoundError as e:
        print(f"Error: {e}")
        print("Make sure FFplay is installed: brew install ffmpeg")
        sys.exit(1)

if __name__ == '__main__':
    main()