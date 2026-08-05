#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data* thread_args = (struct thread_data *) thread_param;
    
    printf("\n===================================\n");
    printf("WAITSTART: %d\n", thread_args->waitStart*1000);
    printf("WAITRELEASE: %d\n", thread_args->waitRelease*1000);
    printf("===================================\n");
    
    // Wait "wait_to_obtain_ms" milliseconds
    usleep(thread_args->waitStart*1000);
    
    // Attempt to lock thread with mutex
    int threadLock = pthread_mutex_lock(thread_args->thread_lock);
    
    // Return failure if mutex lock fails
    if (threadLock != 0) {
        thread_args->thread_complete_success = false;
        return thread_args;
    }
    
    // Wait "wait_to_release_ms" milliseconds
    usleep(thread_args->waitRelease*1000);
    
    // Attempt to unlock thread
    int threadUnlock = pthread_mutex_unlock(thread_args->thread_lock);
    
    // Return failure if mutex unlocking fails
    if (threadUnlock != 0) {
        thread_args->thread_complete_success = false;
        return thread_args;
    }

    // If nothing failed, return success!
    thread_args->thread_complete_success = true;

    return thread_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,
    int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     *? TODO: allocate memory for thread_data, setup mutex and wait arguments,
     *? pass thread_data to created thread using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // Thread state
    bool threadSuccess = true;
    // Allocate memory for struct within scope
    struct thread_data * thread_data = malloc(sizeof(struct thread_data));
    // Verify that the memory could be allocated, otherwise fail
    if (thread_data == NULL) {
        return false;
    }
    
    // Map the provided arguments into the corresponding variables in the struct
    thread_data->thread_lock = mutex;
    thread_data->waitStart = wait_to_obtain_ms;
    thread_data->waitRelease = wait_to_release_ms;
    thread_data->thread_complete_success = true;

    // Attempt to create the thread
    int threadStatus = pthread_create(thread, NULL, &threadfunc, thread_data);
    // If the thread fails to create, return a failure and print to stderr
    if (threadStatus != 0) {
        fprintf(stderr, "THREAD(s) NOT CREATED!");
        free(thread_data);
        threadSuccess = false;
        return 1;
    }

    // Otherwise, return a success!
    return threadSuccess;
}

