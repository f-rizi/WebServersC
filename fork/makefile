CC = gcc
CFLAGS = -O2 -Wall
TARGET = fork_server

all: $(TARGET)

$(TARGET): fork_server.c
	$(CC) $(CFLAGS) -o $(TARGET) fork_server.c

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
