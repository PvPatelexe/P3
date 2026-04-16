#ifndef MYSH_H
#define MYSH_H

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_LINE 4096
#define MAX_TOKENS 256
#define MAX_ARGS 256
#define MAX_CMDS 32

extern char *search_dirs[];

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *infile;
    char *outfile;
} Command;
typedef struct {
    Command cmds[MAX_CMDS];
    int ncmds;
    int should_exit;
} Job;
typedef struct {
    int signaled;
    int signal_num;
    int exit_code;
} Status;
/* helpers */
char *copy_string(const char *s);
void init_job(Job *job);
void free_job(Job *job);
int is_builtin(char *name);
int has_slash(char *s);
int has_star(char *s);

/* input / prompt */
int read_line_fd(int fd, char *line);
void print_prompt(void);
void print_status(Status st);

/* parsing */
int tokenize(char *line, char *tokens[]);
int parse_job(char *tokens[], int ntokens, Job *job);

/* builtins / path */
int find_program(char *name, char *path);
int run_builtin(char *argv[]);
int builtin_cd(char *argv[]);
int builtin_pwd(void);
int builtin_which(char *argv[]);

/* execution */
Status make_status_from_wait(int wstatus);
Status execute_job(Job *job, int interactive_mode, int *exit_shell);
Status execute_parent_builtin(Command *cmd, int interactive_mode, int *exit_shell);

#endif
