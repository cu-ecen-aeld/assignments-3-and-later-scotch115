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

// For internal testing/printing to std
#define DEBUG 0

volatile sig_atomic_t listening = true;
volatile bool daemonMode = false;

// TODO: Initialize linked-list
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
//    pthread_t threadID;
    struct socketThread * thread;
    SLIST_ENTRY(connectionList) entries;
};


void signal_handler(int signal) {
    // Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
    syslog(LOG_INFO, "Caught signal, exiting\n");
    remove("/var/tmp/aesdsocketdata");
    listening = false;
    // Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    exit(EXIT_SUCCESS);
}

static void time_handler(int sig, siginfo_t *si, void *uc) {
    printf("Caught signal %d\n", sig);
}

// TODO: Pass socket function to thread
void* socket_func(void* socket_param) {
    char buffer[1024];
    struct socketThread* socket_thread = (struct socketThread *) socket_param;
    if (DEBUG == 1) {
        printf("####################### ENTERED SOCKETFUNC ########################\n");
    }
    memset(buffer, 0, sizeof(buffer));

    while (1) {
        if (DEBUG == 1) {
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
                    if (DEBUG == 1) {
                        printf("COULD NOT LOCK THREAD!\n");
                    }
                    syslog(LOG_ERR, "COULD NOT LOCK THREAD!\n");
                } else {
                     if (DEBUG == 1) {
                        printf("LOCKED THREAD DURING WRITE!\n");
                    }
                }
                numWritten += fwrite(buffer + numWritten, 1, bytes - numWritten, socket_thread->filePointer);
                int threadUnlock = pthread_mutex_unlock(socket_thread->thread_lock);
                if (threadUnlock != 0) {
                    if (DEBUG == 1) {
                        printf("COULD NOT UNLOCK THREAD!\n");
                    }
                    syslog(LOG_ERR, "COULD NOT UNLOCK THREAD!\n");
                } else {
                    if (DEBUG == 1) {
                        printf("UNLOCKED THREAD AFTER WRITE!\n");
                    }
                }
            }
            fflush(socket_thread->filePointer);
            // int flush = fflush(socket_thread->filePointer);
            // if (flush != 0) {
            //     printf("FAILED TO FLUSH BUFFERED DATA!\n");
            // } else {
            //     printf("FLUSHED BUFFERED DATA!\n");
            // }
            // Interpret newline characters '\n' as the end of each packet
            char *eol = strchr(buffer, '\n');
            if (eol != NULL) {
                int bytesRead;
                fflush(socket_thread->filePointer);
                rewind(socket_thread->filePointer);
                // Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes
                while((bytesRead = fread(buffer, 1, 1024, socket_thread->filePointer)) > 0) {
                    send(socket_thread->clientfd, buffer, bytesRead, 0);
                }
                // TODO: Set thread complete flag before exiting
                socket_thread->thread_complete = true;
                close(socket_thread->clientfd);
                // Logs message to the syslog "Closed connection from XXXX" 
                printf("\nClosed connection from %s\n", socket_thread->host);
                syslog(LOG_INFO, "\nClosed connection from %s\n", socket_thread->host);
                // Reset to begin accepting new connections from clients in a "forever loop", until a SIGINT or SIGTERM is received
                break;
            }
        }
    }
    if (DEBUG == 1) {
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
    
    /////// [IN PROGRESS] TODO: Initialize RFC 2822 compliant Timer //////////////////////////
//    struct timespec ts;
//    timer_t timerid;
//    struct sigaction timer_sa;
//    struct sigevent timer_sev;
//    struct itimerspec timer_spec;
////    #define SIG SIGRTMIN
////
//    timer_sa.sa_flags = SA_SIGINFO;
//    timer_sa.sa_sigaction = time_handler;
//    timer_sev.sigev_notify = SIGEV_SIGNAL;
//    timer_sev.sigev_signo = SIGUSR1;
//    sigemptyset(&timer_sa.sa_mask);
////    timer_sev.sigev_value.sival_ptr = &timerid;
//    if (timer_create(CLOCK_REALTIME, &timer_sev, &timerid) == -1) {
//        err(EXIT_FAILURE, "Could not create a timer.\n");
//    }
////
////    printf("Timer ID is %#jx\n", (uint64_t) timerid);
//    // One of these is going to do what I want...
//    timer_spec.it_value.tv_sec = 10; 
//    timer_spec.it_value.tv_nsec = 0;
//    timer_spec.it_interval.tv_sec = 10;
//    timer_spec.it_interval.tv_nsec = 0;
//
//    if (timer_settime(timerid, 0, &timer_spec, NULL) == -1) {
//        perror("timer_settime");
//        exit(EXIT_FAILURE);
//    }

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
    FILE *fptr;
    fptr = fopen("/var/tmp/aesdsocketdata", "w+");
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

        // TODO: Implement a check to determine if the timer has incremented by 10 seconds, then write the timestamp to the /var/tmp/aesdsocketdata file
        // --        
        struct sockaddr clientAddress;
        socklen_t clientAddressLen = sizeof(clientAddress);
        int client = accept(_socketfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
        char host[NI_MAXHOST];

        // NOTE: Thread flow recommendation: main > [accept new conn] > [create thread]
               // > {add to list, then for each thread in list > (is thread complete) ? pthread_join to free memory : continue listening accept}
        // Logs message to the syslog "Accepted connection from XXXX", where XXXX is the IP Address of the connected client
        if (client != -1) {
            getnameinfo((struct sockaddr *)&clientAddress, clientAddressLen, host, sizeof(host), NULL, 0, NI_NUMERICHOST); 
            syslog(LOG_INFO, "Accepted connection from %s\n", host);
            printf("Accepted connection from %s\n", host);
        }    


 //////////////////// TODO: CREATE NEW THREAD HERE ///////////////////////////////////////////////////////        
 /////////////////////////////////  THREAD FUNCTION ////////////////////////////////////////////// 
///////////////////////////////////////////////////////////////////////////////////////////////////////
        // NOTE: If any threads have completed use pthread_join() to join the new thread to an existing
        //       thread ID instead of creating a new one (and to avoid memory leaks)
        // Allocate memory for struct within scope
        pthread_t thread; // = malloc(sizeof(pthread_t));
        pthread_mutex_t mutex;
        pthread_mutex_init(&mutex, NULL);
        
        struct socketThread * sockThread = malloc(sizeof(struct socketThread));
          
        // Return false if the memory cannot be allocated for the socket thread
        if (sockThread == NULL) {
            printf("COULD NOT ALLOCATE MEMORY FOR SOCKET THREAD\n");
            return false;
        }

        // TODO: Update thread creation code (check examples)
        sockThread->thread_lock = &mutex;
        sockThread->thread_complete = false;
        sockThread->clientfd = client;
        sockThread->filePointer = fptr;
        sockThread->host = host;
        sockThread->thread = &thread;
        sockThread->thread_id = threadNum;
        
        // TODO: ADD THREAD/PROCESS ID TO SLIST //
        int threadStatus = pthread_create(&thread, NULL, &socket_func, sockThread);
        if (threadStatus != 0) {
            fprintf(stderr, "THREAD(s) NOT CREATED!");
            free(sockThread);
            return 1; // TODO: Confirm that this fails correctly? Yk what I mean...
        }
        
        fprintf(stdout, "Thread created with ID - %d\n", threadNum);
        
 //////////////////// TODO: Add newly-created socket-thread to connectionList, and check for any other socket-threads that can be freed //////////////////
        // Only update connectionList IF a new thread was created.
        struct connectionList * connection = malloc(sizeof(struct connectionList));
        if (connection == NULL) {
            perror("Could not allocate memory for connectionList!");
            return 1;
        }
        
        struct connectionList *cur;

        // connection->threadID = threadNum;
        connection->thread = sockThread;
        
        SLIST_INSERT_HEAD(&head, connection, entries);
        
        threadNum += 1; // Update thread number only after adding the previously created thread to the linked list.
        
        SLIST_FOREACH(cur, &head, entries) {
            if (DEBUG == 1) {
                printf("THREAD ID: %d\n", cur->thread->thread_id);
                printf("THREAD COMPLETE: %d\n", cur->thread->thread_complete);
            }
            if (cur->thread->thread_complete == 1 ) {
                // TODO: If thread finished, free memory with pthread_join()
                pthread_join(*cur->thread->thread, NULL); 
                // printf("Joining/freeing Thread_%d!\n", cur->thread->thread_id);
             }
        }
    }
    //? TODO - Free SLIST?
    // Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    return 0;
}
