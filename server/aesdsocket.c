#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int _socket, _socketfd;
    char *host = NULL;
    char *port = "9000";
    _socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    struct addrinfo info;
    struct addrinfo *result, *rp;

    // if (argc <= 2) {
    //     host = "localhost";
    //     port = "9000";
    // } else {
    //     host = argv[2];
    //     port = argv[3];
    // }
    memset(&info, 0, sizeof(info));
    info.ai_family = AF_INET;
    info.ai_socktype = SOCK_STREAM;
    info.ai_protocol = 0;
    info.ai_flags = AI_PASSIVE;
    info.ai_next = NULL;
    
    // TODO: Open a stream socket bound to port 9000, returning -1 if any socket connections fail
     _socket = getaddrinfo(host, port, &info, &result);
    if (_socket != 0) {
        printf("%s\n", gai_strerror(_socket));
        printf("host: %s, port: %s, info %s, result: %s\n", host, port, &info, &result);
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }
    
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        _socketfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (_socketfd == -1) {
            continue;
        }

        if (bind(_socketfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // success!
        }

        close(_socketfd);
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "Could not bind!\n");
        exit(EXIT_FAILURE);
    }

    // TODO: Listens for and accepts connections
    for (;;) {
        printf("Listening for a connection...\n");
        usleep(10*100000);
    }

    // TODO: Logs message to the syslog "Accepted connection from XXXX", where XXXX is the IP Address of the connected client

    // TODO: Receives data over the connection and appends it to the file at /var/tmp/aesdsocketdata (creating this file if it doesn't exist)
    
    // TODO: Interpret newline characters '\n' as the end of each packet
    
    // TODO: Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes
    
    // TODO: Logs message to the syslog "Closed connection from XXXX" 
    
    // TODO: Reset to begin accepting new connections from clients in a "forever loop", until a SIGINT or SIGTERM is received
    
    // TODO: Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    
    // TODO: Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
}
