#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "mysh.h"

char *copy_string(const char *s) {
    char *p = malloc(strlen(s) + 1);
    if (p == NULL) {
        perror("malloc");
        exit(1);
    }
    strcpy(p, s);
    return p;
}

int has_slash(char *s) {
    return strchr(s, '/') != NULL;
}

int has_star(char *s) {
    return strchr(s, '*') != NULL;
}

void init_job(Job *job) {
    int i, j;
    job->ncmds = 0;
    job->should_exit = 0;

    for (i = 0; i < MAX_CMDS; i++) {
        job->cmds[i].argc = 0;
        job->cmds[i].infile = NULL;
        job->cmds[i].outfile = NULL;
        for (j = 0; j < MAX_ARGS; j++) {
            job->cmds[i].argv[j] = NULL;
        }
    }
}

void free_job(Job *job) {
    int i, j;
    for (i = 0; i < job->ncmds; i++) {
        for (j = 0; j < job->cmds[i].argc; j++) {
            free(job->cmds[i].argv[j]);
        }
        if (job->cmds[i].infile != NULL) {
            free(job->cmds[i].infile);
        }
        if (job->cmds[i].outfile != NULL) {
            free(job->cmds[i].outfile);
        }
    }
    job->ncmds = 0;
    job->should_exit = 0;
}

int is_builtin(char *name) {
    if (name == NULL) return 0;

    return strcmp(name, "cd") == 0 ||
           strcmp(name, "pwd") == 0 ||
           strcmp(name, "which") == 0 ||
           strcmp(name, "exit") == 0;
}


int read_line_fd(int fd, char *line) {
    static char buf[MAX_LINE];
    static int start = 0;
    static int end = 0;
    static int saved_fd = -1;
    int pos = 0;

    if (saved_fd != fd) {
        start = 0;
        end = 0;
        saved_fd = fd;
    }

    while (1) {
        int i;

        for (i = start; i < end; i++) {
            if (buf[i] == '\n') {
                int len = i - start;
                if (pos + len >= MAX_LINE) {
                    len = MAX_LINE - pos - 1;
                }
                memcpy(line + pos, buf + start, len);
                pos += len;
                line[pos] = '\0';
                start = i + 1;
                return 1;
            }
        }

        if (start < end) {
            int len = end - start;
            if (pos + len >= MAX_LINE) {
                len = MAX_LINE - pos - 1;
            }
            memcpy(line + pos, buf + start, len);
            pos += len;
            start = end;
        }

        {
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) return -1;
            if (n == 0) {
                if (pos == 0) return 0;
                line[pos] = '\0';
                return 1;
            }
            start = 0;
            end = n;
        }
    }
}

void print_prompt(void) {
    char cwd[PATH_MAX];
    char *home = getenv("HOME");

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        printf("?$ ");
        fflush(stdout);
        return;
    }

    if (home != NULL) {
        int len = strlen(home);
        if (strncmp(cwd, home, len) == 0 &&
            (cwd[len] == '\0' || cwd[len] == '/')) {
            if (cwd[len] == '\0') {
                printf("~$ ");
            } else {
                printf("~%s$ ", cwd + len);
            }
            fflush(stdout);
            return;
        }
    }

    printf("%s$ ", cwd);
    fflush(stdout);
}

void print_status(Status st) {
    if (st.signaled) {
        char *msg = strsignal(st.signal_num);
        if (msg != NULL) {
            printf("Terminated by signal %d (%s)\n", st.signal_num, msg);
        } else {
            printf("Terminated by signal %d\n", st.signal_num);
        }
    } else if (st.exit_code != 0) {
        printf("Exited with status %d\n", st.exit_code);
    }
}