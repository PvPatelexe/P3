#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "mysh.h"

static int match_pattern(char *name, char *pattern) {
    char *star = strchr(pattern, '*');

    if (star == NULL) {
        return strcmp(name, pattern) == 0;
    }

    {
        int prefix_len = (int)(star - pattern);
        char *suffix = star + 1;
        int suffix_len = strlen(suffix);
        int name_len = strlen(name);

        if (pattern[0] == '*' && name[0] == '.') {
            return 0;
        }

        if (name_len < prefix_len + suffix_len) {
            return 0;
        }

        if (strncmp(name, pattern, prefix_len) != 0) {
            return 0;
        }

        if (suffix_len > 0) {
            if (strcmp(name + name_len - suffix_len, suffix) != 0) {
                return 0;
            }
        }
    }

    return 1;
}

static void sort_strings(char *arr[], int n) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (strcmp(arr[i], arr[j]) > 0) {
                char *temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
    }
}

static void add_expanded_arg(Command *cmd, char *token) {
    char *star;
    char *last_slash;
    char dirpart[PATH_MAX];
    char *pattern;
    char *matches[MAX_ARGS];
    int nmatches = 0;
    DIR *dir;
    struct dirent *ent;
    int i;

    if (!has_star(token)) {
        if (cmd->argc < MAX_ARGS - 1) {
            cmd->argv[cmd->argc++] = copy_string(token);
            cmd->argv[cmd->argc] = NULL;
        }
        return;
    }

    star = strchr(token, '*');
    last_slash = strrchr(token, '/');

    //if * appears before last slash, treat token as literal path
    if (last_slash != NULL && star < last_slash) {
        if (cmd->argc < MAX_ARGS - 1) {
            cmd->argv[cmd->argc++] = copy_string(token);
            cmd->argv[cmd->argc] = NULL;
        }
        return;
    }

    if (last_slash != NULL) {
        int len = (int)(last_slash - token);
        if (len == 0) {
            strcpy(dirpart, "/");
        } 
        else {
            strncpy(dirpart, token, len);
            dirpart[len] = '\0';
        }
        pattern = last_slash + 1;
    } 
    else {
        strcpy(dirpart, ".");
        pattern = token;
    }

    dir = opendir(dirpart);
    if (dir == NULL) {
        if (cmd->argc < MAX_ARGS - 1) {
            cmd->argv[cmd->argc++] = copy_string(token);
            cmd->argv[cmd->argc] = NULL;
        }
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (match_pattern(ent->d_name, pattern)) {   //check if file matches wildcard pattern
            char full[PATH_MAX];

            if (last_slash != NULL) {
                if (strcmp(dirpart, "/") == 0) {
                    snprintf(full, sizeof(full), "/%s", ent->d_name);
                } 
                else {
                    snprintf(full, sizeof(full), "%s/%s", dirpart, ent->d_name);
                }
                matches[nmatches++] = copy_string(full);    //store matched path
            } 
            else {
                matches[nmatches++] = copy_string(ent->d_name); //store matched file name
            }

            if (nmatches >= MAX_ARGS - 1) {
                break;
            }
        }
    }

    closedir(dir);

    if (nmatches == 0) {
        if (cmd->argc < MAX_ARGS - 1) {
            cmd->argv[cmd->argc++] = copy_string(token);
            cmd->argv[cmd->argc] = NULL;
        }
        return;
    }

    sort_strings(matches, nmatches);

    for (i = 0; i < nmatches; i++) {
        if (cmd->argc < MAX_ARGS - 1) {
            cmd->argv[cmd->argc++] = matches[i];
            cmd->argv[cmd->argc] = NULL;
        }
    }
}

int tokenize(char *line, char *tokens[]) { //tokenize input lines
    int ntokens = 0;
    int i = 0;

    while (line[i] != '\0') {
        if (line[i] == '#') {
            line[i] = '\0';
            break;
        }

        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
            line[i] = '\0';
            i++;
            continue;
        }

        if (line[i] == '<' || line[i] == '>' || line[i] == '|') {

            tokens[ntokens++] = &line[i];

            if (line[i + 1] != '\0') {
                line[i + 1] = '\0';
                i += 2;
            } else {
                i++;
            }

            continue;
        }


        tokens[ntokens++] = &line[i];

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

        if (line[i] == '#') {
            line[i] = '\0';
            break;
        }

        if (line[i] == '<' || line[i] == '>' || line[i] == '|') {
            continue;
        }

        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }
    }

    tokens[ntokens] = NULL;
    return ntokens;
}

int parse_job(char *tokens[], int ntokens, Job *job) {
    int i;
    int cmd_index = 0;

    if (ntokens == 0) {
        return 0;
    }

    job->ncmds = 1;

    for (i = 0; i < ntokens; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            job->ncmds++;
        }
    }

    if (job->ncmds > MAX_CMDS) {
        fprintf(stderr, "too many commands in pipeline\n");
        return -1;
    }

    for (i = 0; i < ntokens; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (job->cmds[cmd_index].argc == 0) {
                fprintf(stderr, "syntax error near unexpected token |\n");
                return -1;
            }
            cmd_index++;
            continue;
        }

        if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 >= ntokens ||
                strcmp(tokens[i + 1], "<") == 0 ||
                strcmp(tokens[i + 1], ">") == 0 ||
                strcmp(tokens[i + 1], "|") == 0) {
                fprintf(stderr, "syntax error near <\n");
                return -1;
            }
            if (job->cmds[cmd_index].infile != NULL) {
                fprintf(stderr, "syntax error: multiple input redirections\n");
                return -1;
            }
            job->cmds[cmd_index].infile = copy_string(tokens[i + 1]);
            i++;
            continue;
        }

        if (strcmp(tokens[i], ">") == 0) {    //handle output redirection
            if (i + 1 >= ntokens ||
                strcmp(tokens[i + 1], "<") == 0 ||
                strcmp(tokens[i + 1], ">") == 0 ||
                strcmp(tokens[i + 1], "|") == 0) {
                fprintf(stderr, "syntax error near >\n");
                return -1;
            }
            if (job->cmds[cmd_index].outfile != NULL) {   //make sure single redircts 
                fprintf(stderr, "syntax error: multiple output redirections\n");
                return -1;
            }
            job->cmds[cmd_index].outfile = copy_string(tokens[i + 1]);
            i++;
            continue;
        }

        add_expanded_arg(&job->cmds[cmd_index], tokens[i]);
    }

    for (i = 0; i < job->ncmds; i++) {
        if (job->cmds[i].argc == 0) {
            fprintf(stderr, "syntax error near pipe\n");
            return -1;
        }

        job->cmds[i].argv[job->cmds[i].argc] = NULL;

        if (strcmp(job->cmds[i].argv[0], "exit") == 0) {
            job->should_exit = 1;
        }
    }

    if (job->ncmds > 1) {
        for (i = 0; i < job->ncmds; i++) {
            if (job->cmds[i].infile != NULL || job->cmds[i].outfile != NULL) {
                fprintf(stderr, "syntax error: redirection not allowed in pipeline\n");
                return -1;
            }
        }
    }

    return 1;
}
