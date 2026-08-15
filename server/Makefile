ifdef CROSS_COMPILE
        ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
                CC = aarch64-none-linux-gnu-gcc
        else
                CC = gcc
        endif
else
        CC = gcc
endif

all:
	$(CC) aesdsocket.c -o aesdsocket
default:
	$(CC) aesdsocket.c -o aesdsocket
