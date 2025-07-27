# Makefile.linux for Echolocator v2
CC = gcc
TARGET = echolocator
SRCS = main.c
CFLAGS = -Wall -O2 `sdl2-config --cflags`
LDFLAGS = -lSDL2_mixer -lSDL2_ttf `sdl2-config --libs` -lm

all: $(TARGET)
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
clean:
	rm -f $(TARGET)
