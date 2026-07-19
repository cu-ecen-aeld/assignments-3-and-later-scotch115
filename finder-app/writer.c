#include <stdio.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    
    openlog ("writer", LOG_DEBUG | LOG_ERR, LOG_USER);
    
    for (int i = 0; i < argc; i++) {
        printf("Argument %d is %s\n", i, argv[i]);
    }

    if (argc < 3) {
        printf("Expected 2 arguments and only received %d\n", argc);
        syslog (LOG_ERR, "Expected 2 arguments and only received %d\n", argc);
        return 1;
    }

    FILE *fptr;
    fptr = fopen(argv[1], "w+");

    if (fptr == NULL) {
        printf("ERROR! FILE '%d' COULD NOT BE OPENED", argv[1]);
        syslog (LOG_ERR, "ERROR! FILE '%d' COULD NOT BE OPENED", argv[1]);
    } else {
        printf("Writing %s to %s.\n", argv[2], argv[1]);
        syslog (LOG_DEBUG, "Writing %s to %s.\n", argv[2], argv[1]);
        fprintf(fptr, argv[2]);
        fclose(fptr);
    };
    
    return 0;
};
