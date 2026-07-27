#include "systemcalls.h"
#include "stdlib.h"
#include "unistd.h"
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>


/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

    /*
    * TODO  add your code here
    *  Call the system() function with the command set in the cmd
    *   and return a boolean true if the system() call completed with success
    *   or false() if it returned a failure
    */
    int sysCall = system(cmd);

    if (sysCall != -1) {
        return true;
    } else {
        return false;
    }
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    // Handle non-absolute paths for commands
    if (command[0][0] != '/') {
        return false;
    } else if (command[2][0] != '/') {
        return false;
    }

    /*
    * TODO:
    *   Execute a system command by calling fork, execv(),
    *   and wait instead of system (see LSP page 161).
    *   Use the command[0] as the full path to the command to execute
    *   (first argument to execv), and use the remaining arguments
    *   as second argument to the execv() command.
    *
    */

    fflush(stdout);
    pid_t response = fork();
    
    if (response == -1) {
        perror("Fork failed");
        return false;
    }
    
    if (response == 0) {
        // Run Child PID Process
        execv( command[0], command );
    } else {
        wait( NULL );
    }
  
    va_end(args);
    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


    /*
    * TODO
    *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
    *   redirect standard out to a file specified by outputfile.
    *   The rest of the behaviour is same as do_exec()
    *
    */
    char * filtered_command = malloc(strlen(command[1]) + 3);
    if (!filtered_command) {
        perror("Malloc failed");
        return 1;
    }

    sprintf(filtered_command, "'%s'", command[1]);

    fflush(stdout);
    pid_t child = fork();
    if (child == -1) {
        printf("Error! Fork returned '-1'.");
        exit(EXIT_FAILURE);
    } else {
        int file = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);

        if (file < 0) {
            printf("Error opening %s", outputfile);
            return false;
        }
        dup2(file, 1);
        close(file);

        // Handle non-absolute paths for commands
        if (command[0][0] != '/') {
            return false;
        } else if (command[2][0] != '/') {
            return false;
        }

        printf("FILTER: %s\n", filtered_command);

        int exec_res = execv( command[0], &filtered_command );
        if (exec_res == -1) {
            printf("\n");
            perror("'execv() FAILED WITH ERROR");
            printf("When trying to call '%s'\n\n", *command);
            exit(EXIT_FAILURE);
        } else {
            wait( NULL );
        }
    } 

    va_end(args);
    return true;
}
