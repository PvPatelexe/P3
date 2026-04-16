#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mysh.h"

static int open_input(char *file) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        perror(file);
    }
    return fd;
}
static int open_output(char *file) {
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        perror(file);
    }
    return fd;
}
Status make_status_from_wait(int status) {
    Status st;
    st.signaled = 0;
    st.signal_num = 0;
    st.exit_code = 0;

    if (WIFSIGNALED(status)) {
        st.signaled = 1;
        st.signal_num = WTERMSIG(status);
    } 
    else if (WIFEXITED(status)) {
        st.exit_code = WEXITSTATUS(status);
    } 
    else {
        st.exit_code = 1;
    }
    return st;
}

Status execute_parent_builtin(Command *cmd, int interactive_mode, int *exit_shell) {
    Status st;
    int saved_stdin = -1;
    int saved_stdout = -1;
    int fd;

    st.signaled = 0;
    st.signal_num = 0;
    st.exit_code = 0;

    if (cmd->infile != NULL || !interactive_mode) {
        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin < 0) {   //make sure stdin was duplicated
            perror("dup");
            st.exit_code = 1;
            return st;
        }
        if (cmd->infile != NULL) {   //open specified input file
            fd = open_input(cmd->infile);
            if (fd < 0) {
                close(saved_stdin);
                st.exit_code = 1;
                return st;
            }
        } 
        else {    //no input file logic
            fd = open("/dev/null", O_RDONLY);
            if (fd < 0) {
                perror("/dev/null");
                close(saved_stdin);
                st.exit_code = 1;
                return st;
            }
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            close(saved_stdin);
            st.exit_code = 1;
            return st;
        }
        close(fd);
    }
    if (cmd->outfile != NULL) {
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdout < 0) {
            perror("dup");
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            st.exit_code = 1;
            return st;
        }
        //restore original and return error
        fd = open_output(cmd->outfile);
        if (fd < 0) {
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            close(saved_stdout);   //clean up saved stdout
            st.exit_code = 1;
            return st;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            close(saved_stdout);
            st.exit_code = 1;
            return st;
        }
        close(fd);
    }
    if (strcmp(cmd->argv[0], "exit") == 0) {
        *exit_shell = 1;
        st.exit_code = 0;
    } 
    else {
        st.exit_code = run_builtin(cmd->argv);
    }
    if (saved_stdin >= 0) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    }
    if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
    return st;
}

static void child_run_command(Command *cmd, int in_fd, int out_fd, int interactive_mode, int devnull_fd) {
    char path[PATH_MAX];
    int fd;

    if (in_fd != -1) {    //read input from previous pipe
        dup2(in_fd, STDIN_FILENO);
    } 
    else if (cmd->infile != NULL) {      //use redirected input file
        fd = open_input(cmd->infile);
        if (fd < 0) exit(1);
        dup2(fd, STDIN_FILENO);
        close(fd);
    } 
    else if (!interactive_mode) {
        dup2(devnull_fd, STDIN_FILENO);
    }
    if (out_fd != -1) {
        dup2(out_fd, STDOUT_FILENO);
    } 
    else if (cmd->outfile != NULL) {
        fd = open_output(cmd->outfile);
        if (fd < 0) exit(1);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (in_fd != -1) close(in_fd);
    if (out_fd != -1) close(out_fd);
    if (devnull_fd != -1) close(devnull_fd);

    if (is_builtin(cmd->argv[0])) {
        exit(run_builtin(cmd->argv));
    }
    if (has_slash(cmd->argv[0])) {
        execv(cmd->argv[0], cmd->argv);
        perror(cmd->argv[0]);
        exit(127);
    }
    if (!find_program(cmd->argv[0], path)) {   //return error when not found
        fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
        exit(127);
    }
    execv(path, cmd->argv);
    perror(path);
    exit(127);
}

Status execute_job(Job *job, int interactive_mode, int *exit_shell) {
    Status st;
    int devnull_fd = -1;
    int prev_read = -1;
    pid_t pids[MAX_CMDS];
    int i;

    st.signaled = 0;
    st.signal_num = 0;
    st.exit_code = 0;

    if (job->ncmds == 1 && is_builtin(job->cmds[0].argv[0])) {
        return execute_parent_builtin(&job->cmds[0], interactive_mode, exit_shell);
    }
    //open /dev/null in batch mode so commands dont read from shell input stream
    if (!interactive_mode) {
        devnull_fd = open("/dev/null", O_RDONLY);
        if (devnull_fd < 0) {
            perror("/dev/null");
            st.exit_code = 1;
            return st;
        }
    }
    for (i = 0; i < job->ncmds; i++) {
        int pipefd[2] = {-1, -1};
        if (i < job->ncmds - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                if (prev_read != -1) close(prev_read);
                if (devnull_fd != -1) close(devnull_fd);
                st.exit_code = 1;
                return st;
            }
        }
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            if (prev_read != -1) close(prev_read);
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            if (devnull_fd != -1) close(devnull_fd);
            st.exit_code = 1;
            return st;
        }
        if (pids[i] == 0) {
            if (pipefd[0] != -1) close(pipefd[0]);
            child_run_command(&job->cmds[i],
                              prev_read,
                              (i < job->ncmds - 1) ? pipefd[1] : -1,
                              interactive_mode,
                              devnull_fd);
        }
        if (prev_read != -1) close(prev_read);
        if (pipefd[1] != -1) close(pipefd[1]);
        prev_read = pipefd[0];
    }
    if (prev_read != -1) close(prev_read);
    if (devnull_fd != -1) close(devnull_fd);
    for (i = 0; i < job->ncmds; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == job->ncmds - 1) {
            st = make_status_from_wait(status);
        }
    }
    if (job->should_exit) {
        *exit_shell = 1;
    }
    return st;
}
