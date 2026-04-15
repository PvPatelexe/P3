#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define READ_CHUNK 4096
#define INITIAL_VEC_CAP 8
#define SHELL_FAIL 1

static const char *SEARCH_DIRS[] = {
    "/usr/local/bin",
    "/usr/bin",
    "/bin",
    NULL
};

typedef struct {
    char **items;
    size_t size;
    size_t cap;
} StrVec;

typedef struct {
    char **argv;      // NULL-terminated
    int argc;
    char *infile;     // NULL if none
    char *outfile;    // NULL if none
} Command;

typedef struct {
    Command *cmds;
    int ncmds;
    int should_exit_after;
} Job;

typedef struct {
    int exit_code;   // valid when signaled == 0
    int signaled;    // 1 if terminated by signal
    int signal_num;  // valid when signaled == 1
} CmdStatus;

typedef struct {
    int fd;
    char buf[READ_CHUNK];
    ssize_t start;
    ssize_t end;
    int eof;
} Reader;

/* ========================= Utility ========================= */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) {
        fprintf(stderr, "realloc failed\n");
        exit(EXIT_FAILURE);
    }
    return q;
}

static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = xmalloc(len);
    memcpy(copy, s, len);
    return copy;
}

static int has_slash(const char *s) {
    return strchr(s, '/') != NULL;
}

static int contains_star(const char *s) {
    return strchr(s, '*') != NULL;
}

/* ========================= Dynamic String Vector ========================= */

static void strvec_init(StrVec *v) {
    v->size = 0;
    v->cap = INITIAL_VEC_CAP;
    v->items = xmalloc(v->cap * sizeof(char *));
}

static void strvec_push_owned(StrVec *v, char *s) {
    if (v->size == v->cap) {
        v->cap *= 2;
        v->items = xrealloc(v->items, v->cap * sizeof(char *));
    }
    v->items[v->size++] = s;
}

static void strvec_push_copy(StrVec *v, const char *s) {
    strvec_push_owned(v, xstrdup(s));
}

static void strvec_free(StrVec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->size; i++) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->size = 0;
    v->cap = 0;
}

/* ========================= Reader using read() ========================= */

static void reader_init(Reader *r, int fd) {
    r->fd = fd;
    r->start = 0;
    r->end = 0;
    r->eof = 0;
}

/*
 * Returns:
 *   1 if a line was read into *out_line
 *   0 on EOF with no more data
 *  -1 on read error
 *
 * The returned line is heap-allocated and does NOT include the trailing '\n'.
 */
static int reader_next_line(Reader *r, char **out_line) {
    StrVec chars;
    strvec_init(&chars);

    while (1) {
        /* Search current buffer for newline */
        for (ssize_t i = r->start; i < r->end; i++) {
            if (r->buf[i] == '\n') {
                ssize_t len = i - r->start;
                char *line = xmalloc((size_t)len + 1);

                for (ssize_t j = 0; j < len; j++) {
                    line[j] = r->buf[r->start + j];
                }
                line[len] = '\0';

                r->start = i + 1;
                strvec_free(&chars);
                *out_line = line;
                return 1;
            }
        }

        /* No newline currently in buffer; append remaining bytes to chars */
        if (r->start < r->end) {
            ssize_t remain = r->end - r->start;
            char *chunk = xmalloc((size_t)remain + 1);
            memcpy(chunk, r->buf + r->start, (size_t)remain);
            chunk[remain] = '\0';
            strvec_push_owned(&chars, chunk);
            r->start = r->end;
        }

        if (r->eof) {
            if (chars.size == 0) {
                strvec_free(&chars);
                return 0;
            }

            size_t total = 0;
            for (size_t i = 0; i < chars.size; i++) {
                total += strlen(chars.items[i]);
            }

            char *line = xmalloc(total + 1);
            size_t pos = 0;
            for (size_t i = 0; i < chars.size; i++) {
                size_t part = strlen(chars.items[i]);
                memcpy(line + pos, chars.items[i], part);
                pos += part;
            }
            line[pos] = '\0';

            strvec_free(&chars);
            *out_line = line;
            return 1;
        }

        ssize_t n = read(r->fd, r->buf, sizeof(r->buf));
        if (n < 0) {
            strvec_free(&chars);
            return -1;
        }
        if (n == 0) {
            r->eof = 1;
            r->start = 0;
            r->end = 0;
        } else {
            r->start = 0;
            r->end = n;
        }
    }
}

/* ========================= Prompt / Status ========================= */

static void print_prompt(void) {
    char cwd[PATH_MAX];
    char *home = getenv("HOME");

    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "?");
    }

    if (home) {
        size_t home_len = strlen(home);
        if (strncmp(cwd, home, home_len) == 0 &&
            (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
            if (cwd[home_len] == '\0') {
                dprintf(STDOUT_FILENO, "~$ ");
            } else {
                dprintf(STDOUT_FILENO, "~%s$ ", cwd + home_len);
            }
            return;
        }
    }

    dprintf(STDOUT_FILENO, "%s$ ", cwd);
}

static void print_interactive_status(CmdStatus st) {
    if (st.signaled) {
        const char *name = strsignal(st.signal_num);
        if (name) {
            dprintf(STDOUT_FILENO, "Terminated by signal %d (%s)\n", st.signal_num, name);
        } else {
            dprintf(STDOUT_FILENO, "Terminated by signal %d\n", st.signal_num);
        }
    } else if (st.exit_code != 0) {
        dprintf(STDOUT_FILENO, "Exited with status %d\n", st.exit_code);
    }
}

/* ========================= Tokenization ========================= */

static void free_tokens(StrVec *tokens) {
    strvec_free(tokens);
}

static void tokenize_line(const char *line, StrVec *tokens) {
    strvec_init(tokens);

    size_t i = 0;
    while (line[i] != '\0') {
        if (line[i] == '#') {
            break;
        }

        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
            i++;
            continue;
        }

        if (line[i] == '<' || line[i] == '>' || line[i] == '|') {
            char op[2];
            op[0] = line[i];
            op[1] = '\0';
            strvec_push_copy(tokens, op);
            i++;
            continue;
        }

        size_t start = i;
        while (line[i] != '\0' &&
               line[i] != ' ' &&
               line[i] != '\t' &&
               line[i] != '\r' &&
               line[i] != '<' &&
               line[i] != '>' &&
               line[i] != '|' &&
               line[i] != '#') {
            i++;
        }

        size_t len = i - start;
        char *tok = xmalloc(len + 1);
        memcpy(tok, line + start, len);
        tok[len] = '\0';
        strvec_push_owned(tokens, tok);
    }
}

/* ========================= Wildcard Expansion ========================= */

static int match_pattern_name(const char *name, const char *pattern) {
    const char *star = strchr(pattern, '*');
    if (!star) {
        return strcmp(name, pattern) == 0;
    }

    size_t prefix_len = (size_t)(star - pattern);
    const char *suffix = star + 1;
    size_t suffix_len = strlen(suffix);
    size_t name_len = strlen(name);

    if (pattern[0] == '*' && name[0] == '.') {
        return 0;
    }

    if (name_len < prefix_len + suffix_len) {
        return 0;
    }

    if (strncmp(name, pattern, prefix_len) != 0) {
        return 0;
    }

    if (suffix_len > 0 &&
        strcmp(name + name_len - suffix_len, suffix) != 0) {
        return 0;
    }

    return 1;
}

static int cmp_strings(const void *a, const void *b) {
    const char * const *sa = a;
    const char * const *sb = b;
    return strcmp(*sa, *sb);
}

static void expand_one_token(StrVec *out, const char *token) {
    if (!contains_star(token)) {
        strvec_push_copy(out, token);
        return;
    }

    const char *star = strchr(token, '*');
    const char *last_slash = strrchr(token, '/');

    if (last_slash && star < last_slash) {
        /* Only allow * in the file name / last path component */
        strvec_push_copy(out, token);
        return;
    }

    char dirpart[PATH_MAX];
    const char *pattern;

    if (last_slash) {
        size_t dirlen = (size_t)(last_slash - token);
        if (dirlen == 0) {
            strcpy(dirpart, "/");
        } else {
            if (dirlen >= sizeof(dirpart)) {
                strvec_push_copy(out, token);
                return;
            }
            memcpy(dirpart, token, dirlen);
            dirpart[dirlen] = '\0';
        }
        pattern = last_slash + 1;
    } else {
        strcpy(dirpart, ".");
        pattern = token;
    }

    DIR *dir = opendir(dirpart);
    if (!dir) {
        strvec_push_copy(out, token);
        return;
    }

    StrVec matches;
    strvec_init(&matches);

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (match_pattern_name(ent->d_name, pattern)) {
            char full[PATH_MAX];
            if (last_slash) {
                if (strcmp(dirpart, "/") == 0) {
                    snprintf(full, sizeof(full), "/%s", ent->d_name);
                } else {
                    snprintf(full, sizeof(full), "%s/%s", dirpart, ent->d_name);
                }
                strvec_push_copy(&matches, full);
            } else {
                strvec_push_copy(&matches, ent->d_name);
            }
        }
    }

    closedir(dir);

    if (matches.size == 0) {
        strvec_free(&matches);
        strvec_push_copy(out, token);
        return;
    }

    qsort(matches.items, matches.size, sizeof(char *), cmp_strings);

    for (size_t i = 0; i < matches.size; i++) {
        strvec_push_copy(out, matches.items[i]);
    }

    strvec_free(&matches);
}

/* ========================= Job Parsing ========================= */

static void free_command(Command *cmd) {
    if (!cmd) return;

    if (cmd->argv) {
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
    }
    free(cmd->infile);
    free(cmd->outfile);

    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->infile = NULL;
    cmd->outfile = NULL;
}

static void free_job(Job *job) {
    if (!job) return;
    for (int i = 0; i < job->ncmds; i++) {
        free_command(&job->cmds[i]);
    }
    free(job->cmds);
    job->cmds = NULL;
    job->ncmds = 0;
    job->should_exit_after = 0;
}

static int is_builtin_name(const char *name) {
    return strcmp(name, "cd") == 0 ||
           strcmp(name, "pwd") == 0 ||
           strcmp(name, "which") == 0 ||
           strcmp(name, "exit") == 0;
}

static int parse_tokens_into_job(const StrVec *tokens, Job *job) {
    job->cmds = NULL;
    job->ncmds = 0;
    job->should_exit_after = 0;

    if (tokens->size == 0) {
        return 0; /* empty command */
    }

    int max_cmds = 1;
    for (size_t i = 0; i < tokens->size; i++) {
        if (strcmp(tokens->items[i], "|") == 0) {
            max_cmds++;
        }
    }

    job->cmds = calloc((size_t)max_cmds, sizeof(Command));
    if (!job->cmds) {
        fprintf(stderr, "calloc failed\n");
        exit(EXIT_FAILURE);
    }

    int cmd_index = 0;
    StrVec words;
    strvec_init(&words);
    int saw_word_in_this_cmd = 0;

    for (size_t i = 0; i < tokens->size; i++) {
        const char *tok = tokens->items[i];

        if (strcmp(tok, "|") == 0) {
            if (!saw_word_in_this_cmd) {
                fprintf(stderr, "syntax error near unexpected token |\n");
                strvec_free(&words);
                free_job(job);
                return -1;
            }

            StrVec expanded;
            strvec_init(&expanded);
            for (size_t j = 0; j < words.size; j++) {
                expand_one_token(&expanded, words.items[j]);
            }

            Command *cmd = &job->cmds[cmd_index];
            cmd->argc = (int)expanded.size;
            cmd->argv = xmalloc(((size_t)cmd->argc + 1) * sizeof(char *));
            for (int j = 0; j < cmd->argc; j++) {
                cmd->argv[j] = xstrdup(expanded.items[j]);
            }
            cmd->argv[cmd->argc] = NULL;

            if (cmd->argc > 0 && strcmp(cmd->argv[0], "exit") == 0) {
                job->should_exit_after = 1;
            }

            strvec_free(&expanded);
            strvec_free(&words);
            strvec_init(&words);

            saw_word_in_this_cmd = 0;
            cmd_index++;
            continue;
        }

        if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0) {
            if (i + 1 >= tokens->size) {
                fprintf(stderr, "syntax error near unexpected end of line\n");
                strvec_free(&words);
                free_job(job);
                return -1;
            }

            const char *next = tokens->items[i + 1];
            if (strcmp(next, "<") == 0 || strcmp(next, ">") == 0 || strcmp(next, "|") == 0) {
                fprintf(stderr, "syntax error near unexpected token %s\n", next);
                strvec_free(&words);
                free_job(job);
                return -1;
            }

            Command *cmd = &job->cmds[cmd_index];
            if (strcmp(tok, "<") == 0) {
                if (cmd->infile) {
                    fprintf(stderr, "syntax error: multiple input redirections\n");
                    strvec_free(&words);
                    free_job(job);
                    return -1;
                }
                cmd->infile = xstrdup(next);
            } else {
                if (cmd->outfile) {
                    fprintf(stderr, "syntax error: multiple output redirections\n");
                    strvec_free(&words);
                    free_job(job);
                    return -1;
                }
                cmd->outfile = xstrdup(next);
            }

            i++;
            continue;
        }

        strvec_push_copy(&words, tok);
        saw_word_in_this_cmd = 1;
    }

    if (!saw_word_in_this_cmd) {
        fprintf(stderr, "syntax error near unexpected token |\n");
        strvec_free(&words);
        free_job(job);
        return -1;
    }

    StrVec expanded;
    strvec_init(&expanded);
    for (size_t j = 0; j < words.size; j++) {
        expand_one_token(&expanded, words.items[j]);
    }

    Command *cmd = &job->cmds[cmd_index];
    cmd->argc = (int)expanded.size;
    cmd->argv = xmalloc(((size_t)cmd->argc + 1) * sizeof(char *));
    for (int j = 0; j < cmd->argc; j++) {
        cmd->argv[j] = xstrdup(expanded.items[j]);
    }
    cmd->argv[cmd->argc] = NULL;

    if (cmd->argc > 0 && strcmp(cmd->argv[0], "exit") == 0) {
        job->should_exit_after = 1;
    }

    strvec_free(&expanded);
    strvec_free(&words);

    job->ncmds = cmd_index + 1;

    /* For simplicity per spec, reject any redirection inside a pipeline */
    if (job->ncmds > 1) {
        for (int k = 0; k < job->ncmds; k++) {
            if (job->cmds[k].infile || job->cmds[k].outfile) {
                fprintf(stderr, "syntax error: redirection not allowed in pipeline\n");
                free_job(job);
                return -1;
            }
        }
    }

    if (job->ncmds == 1 && job->cmds[0].argc == 0) {
        free_job(job);
        return 0;
    }

    return 1;
}

/* ========================= Built-ins ========================= */

static int search_program_path(const char *name, char *out, size_t outsz) {
    if (!name || !out || outsz == 0) return 0;

    if (has_slash(name)) {
        if (access(name, X_OK) == 0) {
            snprintf(out, outsz, "%s", name);
            return 1;
        }
        return 0;
    }

    if (is_builtin_name(name)) {
        return 0;
    }

    for (int i = 0; SEARCH_DIRS[i] != NULL; i++) {
        snprintf(out, outsz, "%s/%s", SEARCH_DIRS[i], name);
        if (access(out, X_OK) == 0) {
            return 1;
        }
    }

    return 0;
}

static int builtin_cd(char **argv) {
    if (!argv[1]) {
        char *home = getenv("HOME");
        if (!home) {
            errno = ENOENT;
            perror("cd");
            return SHELL_FAIL;
        }
        if (chdir(home) != 0) {
            perror("cd");
            return SHELL_FAIL;
        }
        return 0;
    }

    if (argv[2]) {
        fprintf(stderr, "cd: too many arguments\n");
        return SHELL_FAIL;
    }

    if (chdir(argv[1]) != 0) {
        perror("cd");
        return SHELL_FAIL;
    }

    return 0;
}

static int builtin_pwd(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("pwd");
        return SHELL_FAIL;
    }
    dprintf(STDOUT_FILENO, "%s\n", cwd);
    return 0;
}

static int builtin_which(char **argv) {
    if (!argv[1] || argv[2]) {
        return SHELL_FAIL;
    }

    if (is_builtin_name(argv[1])) {
        return SHELL_FAIL;
    }

    char path[PATH_MAX];
    if (!search_program_path(argv[1], path, sizeof(path))) {
        return SHELL_FAIL;
    }

    dprintf(STDOUT_FILENO, "%s\n", path);
    return 0;
}

static int run_builtin(char **argv) {
    if (!argv[0]) return 0;

    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(argv);
    }
    if (strcmp(argv[0], "pwd") == 0) {
        return builtin_pwd();
    }
    if (strcmp(argv[0], "which") == 0) {
        return builtin_which(argv);
    }
    if (strcmp(argv[0], "exit") == 0) {
        return 0;
    }

    return SHELL_FAIL;
}

/* ========================= Redirection / Exec ========================= */

static int open_input_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
    }
    return fd;
}

static int open_output_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        perror(path);
    }
    return fd;
}

static void child_setup_stdio(Command *cmd,
                              int input_fd,
                              int output_fd,
                              int interactive_mode,
                              int devnull_fd) {
    if (input_fd >= 0) {
        if (dup2(input_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
    } else if (cmd->infile) {
        int fd = open_input_file(cmd->infile);
        if (fd < 0) _exit(1);
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            _exit(1);
        }
        close(fd);
    } else if (!interactive_mode) {
        if (dup2(devnull_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
    }

    if (output_fd >= 0) {
        if (dup2(output_fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
    } else if (cmd->outfile) {
        int fd = open_output_file(cmd->outfile);
        if (fd < 0) _exit(1);
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            _exit(1);
        }
        close(fd);
    }
}

static void exec_external_or_fail(char **argv) {
    char path[PATH_MAX];

    if (has_slash(argv[0])) {
        execv(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    if (!search_program_path(argv[0], path, sizeof(path))) {
        fprintf(stderr, "%s: command not found\n", argv[0]);
        _exit(127);
    }

    execv(path, argv);
    perror(path);
    _exit(127);
}

static CmdStatus status_from_wait(int wstatus) {
    CmdStatus st;
    st.exit_code = 0;
    st.signaled = 0;
    st.signal_num = 0;

    if (WIFSIGNALED(wstatus)) {
        st.signaled = 1;
        st.signal_num = WTERMSIG(wstatus);
    } else if (WIFEXITED(wstatus)) {
        st.exit_code = WEXITSTATUS(wstatus);
    } else {
        st.exit_code = SHELL_FAIL;
    }

    return st;
}

/* ========================= Command Execution ========================= */

static CmdStatus execute_single_builtin_in_parent(Command *cmd,
                                                  int interactive_mode,
                                                  int *request_exit) {
    CmdStatus st = {0, 0, 0};

    int saved_stdin = -1;
    int saved_stdout = -1;
    int temp_in = -1;
    int temp_out = -1;
    int devnull = -1;

    if (cmd->infile || !interactive_mode) {
        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin < 0) {
            perror("dup");
            st.exit_code = SHELL_FAIL;
            return st;
        }

        if (cmd->infile) {
            temp_in = open_input_file(cmd->infile);
            if (temp_in < 0) {
                close(saved_stdin);
                st.exit_code = SHELL_FAIL;
                return st;
            }
        } else {
            devnull = open("/dev/null", O_RDONLY);
            if (devnull < 0) {
                perror("/dev/null");
                close(saved_stdin);
                st.exit_code = SHELL_FAIL;
                return st;
            }
            temp_in = devnull;
        }

        if (dup2(temp_in, STDIN_FILENO) < 0) {
            perror("dup2");
            close(saved_stdin);
            close(temp_in);
            st.exit_code = SHELL_FAIL;
            return st;
        }
    }

    if (cmd->outfile) {
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdout < 0) {
            perror("dup");
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            if (temp_in >= 0) close(temp_in);
            st.exit_code = SHELL_FAIL;
            return st;
        }

        temp_out = open_output_file(cmd->outfile);
        if (temp_out < 0) {
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            close(saved_stdout);
            if (temp_in >= 0) close(temp_in);
            st.exit_code = SHELL_FAIL;
            return st;
        }

        if (dup2(temp_out, STDOUT_FILENO) < 0) {
            perror("dup2");
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            close(saved_stdout);
            close(temp_out);
            if (temp_in >= 0) close(temp_in);
            st.exit_code = SHELL_FAIL;
            return st;
        }
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        *request_exit = 1;
        st.exit_code = 0;
    } else {
        st.exit_code = run_builtin(cmd->argv);
    }

    if (saved_stdin >= 0) {
        if (dup2(saved_stdin, STDIN_FILENO) < 0) {
            perror("dup2");
        }
        close(saved_stdin);
    }
    if (saved_stdout >= 0) {
        if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
            perror("dup2");
        }
        close(saved_stdout);
    }
    if (temp_in >= 0) close(temp_in);
    if (temp_out >= 0) close(temp_out);

    return st;
}

static CmdStatus execute_job(Job *job, int interactive_mode, int *request_exit) {
    CmdStatus final_status = {0, 0, 0};

    if (job->ncmds == 0) {
        return final_status;
    }

    Command *first = &job->cmds[0];

    /* Standalone built-ins that must affect shell state execute in parent */
    if (job->ncmds == 1 &&
        first->argc > 0 &&
        is_builtin_name(first->argv[0])) {
        return execute_single_builtin_in_parent(first, interactive_mode, request_exit);
    }

    int devnull_fd = -1;
    if (!interactive_mode) {
        devnull_fd = open("/dev/null", O_RDONLY);
        if (devnull_fd < 0) {
            perror("/dev/null");
            final_status.exit_code = SHELL_FAIL;
            return final_status;
        }
    }

    pid_t *pids = xmalloc((size_t)job->ncmds * sizeof(pid_t));
    int prev_read = -1;

    for (int i = 0; i < job->ncmds; i++) {
        int pipefd[2] = {-1, -1};

        if (i < job->ncmds - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                if (prev_read >= 0) close(prev_read);
                if (devnull_fd >= 0) close(devnull_fd);
                free(pids);
                final_status.exit_code = SHELL_FAIL;
                return final_status;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            if (prev_read >= 0) close(prev_read);
            if (devnull_fd >= 0) close(devnull_fd);
            free(pids);
            final_status.exit_code = SHELL_FAIL;
            return final_status;
        }

        if (pid == 0) {
            if (pipefd[0] >= 0) close(pipefd[0]);

            int in_fd = prev_read;
            int out_fd = (i < job->ncmds - 1) ? pipefd[1] : -1;

            child_setup_stdio(&job->cmds[i], in_fd, out_fd, interactive_mode, devnull_fd);

            if (prev_read >= 0) close(prev_read);
            if (pipefd[1] >= 0) close(pipefd[1]);
            if (devnull_fd >= 0) close(devnull_fd);

            if (job->cmds[i].argc == 0) {
                _exit(0);
            }

            if (is_builtin_name(job->cmds[i].argv[0])) {
                int rc = run_builtin(job->cmds[i].argv);
                _exit(rc);
            }

            exec_external_or_fail(job->cmds[i].argv);
        }

        pids[i] = pid;

        if (prev_read >= 0) close(prev_read);
        if (pipefd[1] >= 0) close(pipefd[1]);
        prev_read = pipefd[0];
    }

    if (prev_read >= 0) close(prev_read);
    if (devnull_fd >= 0) close(devnull_fd);

    for (int i = 0; i < job->ncmds; i++) {
        int wstatus = 0;
        if (waitpid(pids[i], &wstatus, 0) < 0) {
            perror("waitpid");
            final_status.exit_code = SHELL_FAIL;
        } else if (i == job->ncmds - 1) {
            final_status = status_from_wait(wstatus);
        }
    }

    free(pids);

    if (job->should_exit_after) {
        *request_exit = 1;
    }

    return final_status;
}

/* ========================= Main ========================= */

int main(int argc, char **argv) {
    int input_fd = STDIN_FILENO;
    int interactive_mode = 0;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
        interactive_mode = 0;
    } else {
        interactive_mode = isatty(STDIN_FILENO);
    }

    Reader reader;
    reader_init(&reader, input_fd);

    if (interactive_mode) {
        dprintf(STDOUT_FILENO, "Welcome to my shell!\n");
    }

    int request_exit = 0;

    while (!request_exit) {
        if (interactive_mode) {
            print_prompt();
        }

        char *line = NULL;
        int rr = reader_next_line(&reader, &line);
        if (rr < 0) {
            perror("read");
            break;
        }
        if (rr == 0) {
            free(line);
            break;
        }

        StrVec tokens;
        tokenize_line(line, &tokens);
        free(line);

        Job job;
        int parse_result = parse_tokens_into_job(&tokens, &job);
        free_tokens(&tokens);

        if (parse_result < 0) {
            CmdStatus st = {SHELL_FAIL, 0, 0};
            if (interactive_mode) {
                print_interactive_status(st);
            }
            continue;
        }

        if (parse_result == 0) {
            continue;
        }

        CmdStatus st = execute_job(&job, interactive_mode, &request_exit);

        if (interactive_mode) {
            print_interactive_status(st);
        }

        free_job(&job);
    }

    if (interactive_mode) {
        dprintf(STDOUT_FILENO, "Exiting my shell.\n");
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    return EXIT_SUCCESS;
}