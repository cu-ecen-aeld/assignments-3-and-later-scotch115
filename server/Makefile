CROSS_COMPILE ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

all:
	$(CC) $(CFLAGS) $(TARGET).c -o $(TARGET) $(INCLUDES) $(LDFLAGS)

clean:
	rm -f aesdsocket
