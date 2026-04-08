CC = gcc
CFLAGS = -Wall -pthread
TARGET = servidor
SRC = servidor.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)