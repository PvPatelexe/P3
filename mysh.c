#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "mysh.h"

int main(int argc, char *argv[]) {
    int fd = STDIN_FILENO;
    int interactive_mode = 0;
    char line[MAX_LINE];
    char *tokens[MAX_TOKENS];
    int exit = 0;
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [file]\n", argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc == 2) {    //open file for batch mode
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
        interactive_mode = 0;
    } 
    else {     //open interactive mode
        interactive_mode = isatty(STDIN_FILENO);
    }
    if (interactive_mode) {
        printf("Welcome to my shell!\n");
    }
    while (!exit) {
        int r, ntokens, p;
        Job job;
        Status st;

        if (interactive_mode) {
            print_prompt();
        }
        r = read_line_fd(fd, line);
        if (r < 0) {
            perror("read");
            break;
        }
        if (r == 0) {
            break;
        }
        ntokens = tokenize(line, tokens);
        if (ntokens == 0) {
            continue;
        }
        init_job(&job);

        p = parse_job(tokens, ntokens, &job);
        if (p < 0) {   //construction of the failure status
            Status bad;
            bad.signaled = 0;
            bad.signal_num = 0;
            bad.exit_code = 1;

            if (interactive_mode) {  //print error status only in interactive
                print_status(bad);
            }
            free_job(&job);
            continue;
        }
        if (p == 0) {
            free_job(&job);      //fre allocated resources
            continue;
        }
        st = execute_job(&job, interactive_mode, &exit);

        if (interactive_mode) {
            print_status(st);
        }

        free_job(&job);
    }

    if (interactive_mode) {
        printf("Exiting my shell.\n");
    }
    if (fd != STDIN_FILENO) {
        close(fd);
    }
    return EXIT_SUCCESS;
}
