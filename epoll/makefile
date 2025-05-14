CC = gcc
CFLAGS = -O2 -Wall
TARGET = epoll_server

all: $(TARGET)

$(TARGET): epoll_server.c
	$(CC) $(CFLAGS) -o $(TARGET) epoll_server.c

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
