CC = gcc
CFLAGS = -O2 -Wall
TARGET = server_fork

all: $(TARGET)

$(TARGET): server.c
	$(CC) $(CFLAGS) -o $(TARGET) server.c

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
