# Makefile

CC = gcc
CFLAGS = -Wall -g

all: sender receiver

sender: sender.c rtp.h
	$(CC) $(CFLAGS) sender.c -o sender

receiver: receiver.c rtp.h
	$(CC) $(CFLAGS) receiver.c -o receiver

clean:
	rm -f sender receiver
