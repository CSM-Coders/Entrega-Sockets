CC      = gcc
CFLAGS  = -Wall -Wextra -pthread
LDLIBS  = -lm
TARGET  = servidor
SRC     = servidor.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDLIBS)

clean:
	rm -f $(TARGET)
