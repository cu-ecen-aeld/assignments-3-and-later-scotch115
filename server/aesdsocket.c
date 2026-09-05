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
#include <errno.h>
#include <time.h>

// For internal testing/printing to tty
#define DEBUG 0

volatile sig_atomic_t listening = true;
volatile sig_atomic_t timer_setup_complete = 0;
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

void sysPrint(int logLevel, char * message) {
    // Level 0 => LOG_INFO
    // Level 1 => LOG_ERR
    // Level 2 => LOG_DEBUG
    if (logLevel == 0) {
        syslog(LOG_INFO, "%s\n", message);
        printf("%s\n", message);
    } else if (logLevel == 1) {
        syslog(LOG_ERR, "%s\n", message);
        printf("%s\n", message);
    } else if (logLevel == 2) {
        syslog(LOG_DEBUG, "%s\n", message);
        printf("%s\n", message);
    } else {
        syslog(LOG_ERR, "sysPrint() received an invalid 'logLevel' argument!\n");
        fprintf(stderr, "sysPrint() received an invalid 'logLevel' argument!\n");
    }
}

void signal_handler(int signal) {
    // Logs message to syslog "Caught signal, exiting" when SIGINT or SIGTERM are received
    sysPrint(0, "Caught signal, exiting.");
    remove("/var/tmp/aesdsocketdata");
    listening = false;
    // Gracefully exits when SIGINT or SIGTERM are received; completing any open connection operations, closing any open sockets, and DELETING the /var/tmp/aesdsocketdata file
    exit(EXIT_SUCCESS);
}

// New and improved timer thread function
void timestamp(union sigval sigval) {
    (void)sigval;

    char timeStr[1024];
    time_t t;
    struct tm *tmp;
            
    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        perror("localtime");
        exit(EXIT_FAILURE);
    }
    if (strftime(timeStr, 1024, "%a, %d %b %Y %T %z", tmp) == 0) {
        fprintf(stderr, "strftime returned 0\n");
    }
    char * timestamp_str = calloc(1024, sizeof(char));
    strcpy(timestamp_str, "timestamp: ");
    strcat(timestamp_str, timeStr);
    strcat(timestamp_str, "\n");
    int ptr = open("/var/tmp/aesdsocketdata", O_CREAT | O_APPEND | O_RDWR, 0664);
    int locked = pthread_mutex_lock(&mutex);
    if (locked != 0) {
        printf("Failed to lock write thread\n");
    } else {
        printf("Thread locked with mutex\n");
    }
    
    write(ptr, timestamp_str, strlen(timestamp_str));
    int unlocked = pthread_mutex_unlock(&mutex);
    if (unlocked != 0) {
        printf("Failed to unlock write thread\n");
    } else {
            printf("Thread unlocked with mutex\n");
    }
    close(ptr);
    
    return;
}

// Previous implementation was not properly async-safe and was causing issues when trying to run in daemonMode.
// Timer function now properly uses interval timer
int start_timer(timer_t * timer_id, int seconds) {
    if (timer_id == NULL) {
        return -1;
    }

    if (timer_setup_complete == 1) {
        return 0;
    }

    timer_t timerid;
    struct sigevent sev;
    memset(&sev, 0, sizeof(struct sigevent));

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = &timerid;
    sev.sigev_notify_function = &timestamp;
    if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
        perror("Could not start timer!");
        return -1;
    }

    struct itimerspec its;
    its.it_value.tv_sec = seconds;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("Could not set timer interval!");
        return -1;
    }

    timer_setup_complete = 1;
    *timer_id = timerid;
    return 0;
}

// Pass socket function to thread
void * socket_func(void* socket_param) {
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
                    sysPrint(1, "Failed to lock socket thread!");
                } else {
                     if (DEBUG == 2) {
                        printf("Successfully locked socket thread!\n");
                    }
                }
                numWritten += fwrite(buffer + numWritten, 1, bytes - numWritten, socket_thread->filePointer);
                int threadUnlock = pthread_mutex_unlock(socket_thread->thread_lock);
                if (threadUnlock != 0) {
                    sysPrint(1, "Failed to unlock socket thread!");
                } else {
                    if (DEBUG == 2) {
                        printf("Successfully unlocked socket thread!\n");
                    }
                }
            }

            fflush(socket_thread->filePointer);

            // Interpret newline characters '\n' as the end of each packet
            char *eol = strchr(buffer, '\n');
            if (eol != NULL) {
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
                char msg[200];
                memset(msg, 0, sizeof(msg));
                strcpy(msg, "Closed connection from ");
                strcat(msg, socket_thread->host);
            
                sysPrint(0, msg);
                // Reset to begin accepting new connections from clients in a "forever loop", until a SIGINT or SIGTERM is received
                break;
            }
        }
        fclose(socket_thread->filePointer); // Close the file descriptor after the thread completes
    }
    if (DEBUG > 1) {
        printf("####################### EXITED SOCKETFUNC ########################\n");
    }
    return 0;
};

int main(int argc, char *argv[])
{
    openlog ("socket-server", LOG_DEBUG | LOG_ERR, LOG_USER);
    syslog(LOG_INFO, "STARTING AESDSOCKET PROGRAM");
    if (errno != 0) {
        fprintf(stderr, "UNABLE TO OPEN SYSLOG IN AESDSOCKET PROGRAM!!!\n");
    }
    if (argc > 1) {
        // Provide support for running in daemon mode per Assignment 5 Part 2 instructions
        daemonMode = true;
        sysPrint(0, "Started aesdsocket daemon");
   }
    // If `daemonMode` is true, call fork() to create a child/daemon process (re: Assignment 4)
    if (daemonMode) {
        pid_t pid = fork();
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }

        // Additional daemon steps required to properly detach from TTY
        if (setsid() < 0) {
            perror("setsid");
            exit(EXIT_FAILURE);
        }

        chdir("/");

        umask(0);

        int fd = open("/dev/null", O_RDWR);
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
    timer_t timer_id;
    // DAEMON STARTS HERE //
    while(1) {
    
        int _socket, _socketfd;
        char *host = NULL;
        char *port = "9000";
        int reuseaddr = 1;
        struct addrinfo info;
        struct addrinfo *result;
    
        // Initialize thread ID tracking variable
        int threadNum = 0;
        SLIST_HEAD(listHead, connectionList) head;
        SLIST_INIT(&head);
        
        // Initialize new timer function
        start_timer(&timer_id, 10);
    
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
            sysPrint(1, "Could not open '/var/tmp/aesdsocketdata'.");
        } else {
            sysPrint(0, "Writing to '/var/tmp/aesdsocketdata'.");
        }
    
        // Bind socket
        _socketfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (setsockopt(_socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
            sysPrint(1, "Failed to set socket options with 'setsockopt()'");
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
                sysPrint(1, "bind() failed.");
                close(_socketfd);
            }
            if (tryListening != 0) {
                sysPrint(1, "listen() failed.");
            }
            if (_socketfd == -1) {
                sysPrint(1, "_socketfd is -1.");
            }
            close(_socketfd);
            return -1;
        }
    
        fprintf(stdout, "Started listening on %s:%s\n", host, port);
    
        while(1) {
            if (!listening) {
                printf("Quitting\n");
                close(_socketfd);   
            }
    
            if (sigaction(SIGINT, &sigrecv, NULL) == -1) {
                break;
            }
    
            if (sigaction(SIGTERM, &sigrecv, NULL) == -1) {
                break;
            }
          
            struct sockaddr clientAddress;
            socklen_t clientAddressLen = sizeof(clientAddress);
            int client = accept(_socketfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
            char host[NI_MAXHOST];
    
            // Logs message to the syslog "Accepted connection from XXXX", where XXXX is the IP Address of the connected client
            if (client != -1) {
                getnameinfo((struct sockaddr *)&clientAddress, clientAddressLen, host, sizeof(host), NULL, 0, NI_NUMERICHOST); 
                char hostname[100];
                memset(hostname, 0, sizeof(hostname));
                strcpy(hostname, "Accepted connection from: ");
                strcat(hostname, host);
                sysPrint(0, hostname);
            }    
    
            // CREATE NEW THREAD(S)  
            // NOTE: If any threads have completed use pthread_join() to join the new thread to an existing
            //       thread ID instead of creating a new one (and to avoid memory leaks)
            // Allocate memory for struct within scope
            pthread_t thread;
            
            struct socketThread * sockThread = malloc(sizeof(struct socketThread));
              
            // Return false if the memory cannot be allocated for the socket thread
            if (sockThread == NULL) {
                sysPrint(1, "Could not allocate memory for socket thread!");
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
                sysPrint(1, "Thread(s) not created!");
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
                sysPrint(1, "Could not allocate memory for connectionList!");
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
                    if (DEBUG == 2) {
                        printf("Joining/freeing Thread_%d!\n", cur->thread->thread_id);
                    }
                 }
            }
        }
        return 0;
    }
    
    // Syscall to cleanup timer once main function ends
    timer_delete(&timer_id);
        
    return 0;
}
    
