#include <asm-generic/socket.h>
#include <err.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <stdbool.h>

volatile sig_atomic_t listening = true;

void signal_handler(int signal) {
    // TODO: Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
    syslog(LOG_INFO, "Caught signal, exiting\n");
    remove("/var/tmp/aesdsocketdata");
    listening = false;
    // TODO: Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    int _socket, _socketfd, _clientfd;
    char *host = NULL;
    char *port = "9000";
    int reuseaddr = 1;
    struct addrinfo info;
    struct addrinfo *result, *rp;
    openlog ("socket-server", LOG_DEBUG | LOG_ERR, LOG_USER);

    memset(&info, 0, sizeof(info));
    info.ai_family = AF_INET;
    info.ai_socktype = SOCK_STREAM;
    info.ai_protocol = 0;
    info.ai_flags = AI_PASSIVE;
    info.ai_next = NULL;

    struct sigaction sigrecv;
    memset(&sigrecv, 0, sizeof(sigrecv));
    sigrecv.sa_handler = signal_handler;

    fprintf(stdout,"Attempting to start server socket...");
    // TODO: Open a stream socket bound to port 9000, returning -1 if any socket connections fail
     _socket = getaddrinfo(host, port, &info, &result);
    if (_socket != 0) {
        fprintf(stderr, "%s\n", gai_strerror(_socket));
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, "Address info found!\n");
    
    // Setup file pointer and filepath
    FILE *fptr;
    fptr = fopen("/var/tmp/aesdsocketdata", "w+");
    if (fptr == NULL) {
        syslog(LOG_ERR, "ERROR! FILE '%d' COULD NOT BE OPENED!", "/var/tmp/aesdsocketdata");
    } else {
        syslog(LOG_INFO, "WRITING TO FILE");
        fprintf(stdout,"Writing to /var/tmp/aesdsocketdata");
    }

    // Bind socket to 
    _socketfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (setsockopt(_socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        syslog(LOG_ERR, "SETSOCKOPT FAILED");
        freeaddrinfo(result);
        close(_socketfd);
        return -1;
    }
    int tryBind = bind(_socketfd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    // TODO: Listens for and accepts connections
    int tryListening = listen(_socketfd, SOMAXCONN);
    
    if (_socketfd == -1 || tryBind != 0 || tryListening != 0) {
        if (tryBind != 0) {
            syslog(LOG_ERR, "BIND FAILED");
            perror("bind() failed. Error");
            close(_socketfd);
        }
        if (tryListening != 0) {
            syslog(LOG_ERR, "LISTEN FAILED");
            perror("listen() failed. Error");
        }
        if (_socketfd == -1) {
            syslog(LOG_ERR, "SOCKETFD IS -1");
            perror("_socketfd is -1. Error");
        }
        close(_socketfd);
        return -1;
    }
    
    while(1) {
        if (!listening) {
            printf("Quitting\n");
            close(_socketfd);   
        }

        if (sigaction(SIGINT, &sigrecv, NULL) == -1) {
            // TODO: Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
            syslog(LOG_INFO, "Caught SIGINT signal, exiting\n");
            remove("/var/tmp/aesdsocketdata");
        }

        if (sigaction(SIGTERM, &sigrecv, NULL) == -1) {
            // TODO: Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
            syslog(LOG_INFO, "Caught SIGTERM signal, exiting\n");
            remove("/var/tmp/aesdsocketdata");
        }
        
        struct sockaddr clientAddress;
        socklen_t clientAddressLen = sizeof(clientAddress);
        int client = accept(_socketfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
        char host[NI_MAXHOST];
        char buffer[1024];
        
        // TODO: Logs message to the syslog "Accepted connection from XXXX", where XXXX is the IP Address of the connected client
        if (client != -1) {
            getnameinfo((struct sockaddr *)&clientAddress, clientAddressLen, host, sizeof(host), NULL, 0, NI_NUMERICHOST); 
            syslog(LOG_INFO, "Accepted connection from %s\n", host);
        }
        
        memset(buffer, 0, sizeof(buffer));
        while(1) {
            // TODO: Receives data over the connection and appends it to the file at /var/tmp/aesdsocketdata (creating this file if it doesn't exist)
            ssize_t bytes = recv(client, (void*)buffer, 1024, 0);
            if (bytes > 0) {
                printf("Got %ld bytes: %s", sizeof(bytes), buffer);

                int numWritten = 0;
                while (numWritten < bytes) {
                    numWritten += fwrite(buffer + numWritten, 1, bytes - numWritten, fptr);
                }
                fflush(fptr);

                // TODO: Interpret newline characters '\n' as the end of each packet
                char *eol = strchr(buffer, '\n');
                if (eol != NULL) {
                    int bytesRead;

                    fflush(fptr);
                    rewind(fptr);
                    // TODO: Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes
                    while((bytesRead = fread(buffer, 1, 1024, fptr)) > 0) {
                        send(client, buffer, bytesRead, 0);
                    }

                    close(client);

                    // TODO: Logs message to the syslog "Closed connection from XXXX" 
                    printf("Closed connection from %s\n", host);
                    syslog(LOG_INFO, "Closed connection from %s\n", host);
                    // TODO: Reset to begin accepting new connections from clients in a "forever loop", until a SIGINT or SIGTERM is received
                    break;
                }
            }
        }
    }
    // TODO: Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    return 0;
}
