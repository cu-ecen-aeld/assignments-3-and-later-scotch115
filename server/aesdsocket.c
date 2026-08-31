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
#include <pthread.h>
#include <time.h>
#include "queue.h"
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>

// For internal testing/printing to std
#define DEBUG 0

volatile sig_atomic_t listening = true;
volatile sig_atomic_t timer_complete = 0;
volatile bool daemonMode = false;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize linked-list
struct socketThread {
     int thread_id;
     pthread_t * thread;
     pthread_mutex_t * thread_lock;
     int clientfd;
     FILE * filePointer;
     char * host;
     bool thread_complete;
};

struct connectionList {
    struct socketThread * thread;
    SLIST_ENTRY(connectionList) entries;
};

void signal_handler(int signal) {
    // Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
    printf("\nCAUGHT SIGNAL IN SIGNAL HANDLER AND EXITING\n");
    syslog(LOG_INFO, "Caught signal, exiting\n");
    remove("/var/tmp/aesdsocketdata");
    listening = false;
    // Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    exit(EXIT_SUCCESS);
}

void time_handler(int sig) {
    printf("TIMER DONE - WRITE TIMESTAMP\n");
    timer_complete = 1;
    // Reset timer
    signal (sig, time_handler);
    alarm(10);
}

void* timestamp() {
    struct timespec ts;
    while (1) {
        if (timer_complete) {
            timer_complete = 0;
            clock_gettime(CLOCK_REALTIME, &ts);
            char timeStr[1024];
            struct tm *tmp;
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            memset(timeStr, 0, sizeof(timeStr));
            
            tmp = localtime(&ts.tv_sec);
            if (tmp == NULL) {
                perror("localtime");
                exit(EXIT_FAILURE);
            }
            if (strftime(timeStr, 1024, "%a, %d %b %Y %T %z", tmp) == 0) {
                fprintf(stderr, "strftime returned 0\n");
                exit(EXIT_FAILURE);
            }
            char * timestamp_str = calloc(1024, sizeof(char));
            strcpy(timestamp_str, "timestamp: ");
            strcat(timestamp_str, timeStr);
            strcat(timestamp_str, "\n");

            FILE * ptr = fopen("/var/tmp/aesdsocketdata", "a+");
            if (ptr == NULL) {
                syslog(LOG_ERR, "ERROR! FILE '%s' COULD NOT BE OPENED!", "/var/tmp/aesdsocketdata");
            } else {
                syslog(LOG_INFO, "WRITING TO FILE");
                fprintf(stdout,"Writing to /var/tmp/aesdsocketdata\n");
            }
            
            ssize_t bytes = sizeof(timestamp_str);
            int numWritten = 0;

            int locked = pthread_mutex_lock(&mutex);
            if (locked != 0) {
                printf("Failed to lock write thread\n");
            } else {
                printf("Thread locked with mutex\n");
            }
            
            fprintf(ptr, "%s", timestamp_str);

            fflush(ptr);

            // Read from file to ensure file write operation was successful
            int bytesRead;
            fflush(ptr);
            rewind(ptr);
            // Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes
            while((bytesRead = fread(buffer, 1, 1024, ptr)) > 0) {
                printf("FILEBUFFER: %s", buffer);
            }

            int unlocked = pthread_mutex_unlock(&mutex);
            if (unlocked != 0) {
                printf("Failed to unlock write thread\n");
            } else {
                printf("Thread unlocked with mutex\n");
            }

            fclose(ptr);
        }
    }
}

// Pass socket function to thread
void* socket_func(void* socket_param) {
    char buffer[1024];
    struct socketThread* socket_thread = (struct socketThread *) socket_param;
    if (DEBUG > 1) {
        printf("####################### ENTERED SOCKETFUNC ########################\n");
    }
    memset(buffer, 0, sizeof(buffer));
    
    while (1) {
        if (DEBUG == 2) {
            printf("RECEIVING DATA FROM SOCKET INSIDE OF THREAD-SOCKETFUNC\n");
        }
       
        // Receives data over the connection and appends it to the file at /var/tmp/aesdsocketdata (creating this file if it doesn't exist)
        ssize_t bytes = recv(socket_thread->clientfd, (void*)buffer, 1024, 0);
        if (bytes > 0) {
            printf("Got %ld bytes: %s\n", sizeof(bytes), buffer);
            int numWritten = 0;
            while (numWritten < bytes) {
                // Implement mutex around file write operation(s) to prevent access issues
                int threadLock = pthread_mutex_lock(socket_thread->thread_lock);
                if (threadLock !=0) {
                    if (DEBUG == 2) {
                        printf("COULD NOT LOCK THREAD!\n");
                    }
                    syslog(LOG_ERR, "COULD NOT LOCK THREAD!\n");
                } else {
                     if (DEBUG == 2) {
                        printf("LOCKED THREAD DURING WRITE!\n");
                    }
                }
                numWritten += fwrite(buffer + numWritten, 1, bytes - numWritten, socket_thread->filePointer);
                int threadUnlock = pthread_mutex_unlock(socket_thread->thread_lock);
                if (threadUnlock != 0) {
                    if (DEBUG == 2) {
                        printf("COULD NOT UNLOCK THREAD!\n");
                    }
                    syslog(LOG_ERR, "COULD NOT UNLOCK THREAD!\n");
                } else {
                    if (DEBUG == 2) {
                        printf("UNLOCKED THREAD AFTER WRITE!\n");
                    }
                }
            }

            fflush(socket_thread->filePointer);

            // Interpret newline characters '\n' as the end of each packet
            char *eol = strchr(buffer, '\n');
            if (eol != NULL) {
                printf("EOL found\n");
                int bytesRead;
                fflush(socket_thread->filePointer);
                rewind(socket_thread->filePointer);
                // Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes
                while((bytesRead = fread(buffer, 1, 1024, socket_thread->filePointer)) > 0) {
                    printf("FILEBUFFER: %s\n", buffer);
                    send(socket_thread->clientfd, buffer, bytesRead, 0);
                }
                // Set thread complete flag before exiting
                socket_thread->thread_complete = true;
                close(socket_thread->clientfd);
                // Logs message to the syslog "Closed connection from XXXX" 
                printf("\nClosed connection from %s\n", socket_thread->host);
                syslog(LOG_INFO, "\nClosed connection from %s\n", socket_thread->host);
                // Reset to begin accepting new connections from clients in a "forever loop", until a SIGINT or SIGTERM is received
                break;
            }
        }
        fclose(socket_thread->filePointer); // Close the file descriptor after the thread completes
    }
    if (DEBUG > 1) {
        printf("####################### EXITED SOCKETFUNC ########################\n");
    }
};

int main(int argc, char *argv[])
{
    if (argc > 1) {
        // Provide support for running in daemon mode per Assignment 5 Part 2 instructions
        daemonMode = true;
        syslog(LOG_INFO, "Started aesdsocket daemon");
   }
    int _socket, _socketfd, _clientfd;
    char *host = NULL;
    char *port = "9000";
    int reuseaddr = 1;
    struct addrinfo info;
    struct addrinfo *result, *rp;
    openlog ("socket-server", LOG_DEBUG | LOG_ERR, LOG_USER);

    // Initialize thread ID tracking variable
    int threadNum = 0;
    SLIST_HEAD(listHead, connectionList) head;
    SLIST_INIT(&head);
    
    // Initialize the timer and timer handler attributes
    signal(SIGALRM, time_handler);
    alarm(10);
    pthread_t timer_th;
    if (pthread_create(&timer_th, NULL, &timestamp,  NULL) != 0) {
        printf("FAILED TO START TIMER SERVICE!\n");
    } else {
        printf("TIMER SERVICE STARTED.\n");
    }
        
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
    // Open a stream socket bound to port 9000, returning -1 if any socket connections fail
     _socket = getaddrinfo(host, port, &info, &result);
    if (_socket != 0) {
        fprintf(stderr, "%s\n", gai_strerror(_socket));
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, "Address info found!\n");
    
    // Setup file pointer and filepath
    FILE * fptr;
    fptr = fopen("/var/tmp/aesdsocketdata", "a+");
    if (fptr == NULL) {
        syslog(LOG_ERR, "ERROR! FILE '%s' COULD NOT BE OPENED!", "/var/tmp/aesdsocketdata");
    } else {
        syslog(LOG_INFO, "WRITING TO FILE");
        fprintf(stdout,"Writing to /var/tmp/aesdsocketdata\n");
    }

    // Bind socket
    _socketfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (setsockopt(_socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        syslog(LOG_ERR, "SETSOCKOPT FAILED");
        freeaddrinfo(result);
        close(_socketfd);
        return -1;
    }
    int tryBind = bind(_socketfd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    // Listens for and accepts connections
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

    // If `daemonMode` is true, call fork() to create a child/daemon process (re: Assignment 4)
    if (daemonMode) {
        pid_t pid = fork();
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
    }

    fprintf(stdout, "Started listening on %s:%s\n", host, port);

    while(1) {
        if (!listening) {
            printf("Quitting\n");
            close(_socketfd);   
        }

        if (sigaction(SIGINT, &sigrecv, NULL) == -1) {
            // Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
            syslog(LOG_INFO, "Caught SIGINT signal, exiting\n");
            remove("/var/tmp/aesdsocketdata");
        }

        if (sigaction(SIGTERM, &sigrecv, NULL) == -1) {
            // Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
            syslog(LOG_INFO, "Caught SIGTERM signal, exiting\n");
            remove("/var/tmp/aesdsocketdata");
        }
      
        struct sockaddr clientAddress;
        socklen_t clientAddressLen = sizeof(clientAddress);
        int client = accept(_socketfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
        char host[NI_MAXHOST];

        // Logs message to the syslog "Accepted connection from XXXX", where XXXX is the IP Address of the connected client
        if (client != -1) {
            getnameinfo((struct sockaddr *)&clientAddress, clientAddressLen, host, sizeof(host), NULL, 0, NI_NUMERICHOST); 
            syslog(LOG_INFO, "Accepted connection from %s\n", host);
            printf("Accepted connection from %s\n", host);
        }    


        // CREATE NEW THREAD(S)  
        // NOTE: If any threads have completed use pthread_join() to join the new thread to an existing
        //       thread ID instead of creating a new one (and to avoid memory leaks)
        // Allocate memory for struct within scope
        pthread_t thread;
        
        struct socketThread * sockThread = malloc(sizeof(struct socketThread));
          
        // Return false if the memory cannot be allocated for the socket thread
        if (sockThread == NULL) {
            printf("COULD NOT ALLOCATE MEMORY FOR SOCKET THREAD\n");
            return false;
        }

        // Create new thread on socket accept()
        sockThread->thread_lock = &mutex;
        sockThread->thread_complete = false;
        sockThread->clientfd = client;
        sockThread->filePointer = fptr;
        sockThread->host = host;
        sockThread->thread = &thread;
        sockThread->thread_id = threadNum;
        
        // Add thread/process to linked-list
        int threadStatus = pthread_create(&thread, NULL, &socket_func, sockThread);
        if (threadStatus != 0) {
            fprintf(stderr, "THREAD(s) NOT CREATED!");
            free(sockThread);
            return 1;
        }
        
        if (DEBUG == 2) {
            fprintf(stdout, "Thread created with ID - %d\n", threadNum);
        }
        
        // Add newly-created socket-thread to connectionList, and check for any other socket-threads that can be freed
        // Only update connectionList IF a new thread was created.
        struct connectionList * connection = malloc(sizeof(struct connectionList));
        if (connection == NULL) {
            perror("Could not allocate memory for connectionList!");
            return 1;
        }
        
        struct connectionList *cur;
        connection->thread = sockThread;
        
        SLIST_INSERT_HEAD(&head, connection, entries);
        
        threadNum += 1; // Update thread number only after adding the previously created thread to the linked list.
        
        SLIST_FOREACH(cur, &head, entries) {
            if (DEBUG == 2) {
                printf("THREAD ID: %d\n", cur->thread->thread_id);
                printf("THREAD COMPLETE: %d\n", cur->thread->thread_complete);
            }
            if (cur->thread->thread_complete == 1 ) {
                // If thread finished, free memory with pthread_join()
                pthread_join(*cur->thread->thread, NULL); 
                printf("Joining/freeing Thread_%d!\n", cur->thread->thread_id);
             }
        }
    }
    
    return 0;
}
