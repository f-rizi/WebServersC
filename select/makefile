CC = gcc
CFLAGS = -O2 -Wall
TARGET = select_server

all: $(TARGET)

$(TARGET): select_server.c
	$(CC) $(CFLAGS) -o $(TARGET) select_server.c

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
