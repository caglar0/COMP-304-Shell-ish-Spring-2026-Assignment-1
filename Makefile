CC = gcc
CFLAGS = -Wall -Wextra -Wno-sign-compare -g
TARGET = shell-ish
SRC = shellish-skeleton.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

re: clean all
