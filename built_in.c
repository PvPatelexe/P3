#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mysh.h"

char *search_dirs[] = {"/usr/local/bin", "/usr/bin", "/bin", NULL};

int find_program(char *name, char *path) {
    int i;

    if (has_slash(name)) {
        if (access(name, X_OK) == 0) {
            strcpy(path, name);
            return 1;
        }
        return 0;
    }

    if (is_builtin(name)) {
        return 0;
    }

    for (i = 0; search_dirs[i] != NULL; i++) {
        snprintf(path, PATH_MAX, "%s/%s", search_dirs[i], name);
        if (access(path, X_OK) == 0) {
            return 1;
        }
    }

    return 0;
}

int builtin_cd(char *argv[]) {
    char *dir;

    if (argv[1] == NULL) {
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
    } else if (argv[2] != NULL) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    } else {
        dir = argv[1];
    }

    if (chdir(dir) != 0) {
        perror("cd");
        return 1;
    }

    return 0;
}

int builtin_pwd(void) {
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
        return 1;
    }

    printf("%s\n", cwd);
    return 0;
}

int builtin_which(char *argv[]) {
    char path[PATH_MAX];

    if (argv[1] == NULL || argv[2] != NULL) {
        return 1;
    }

    if (is_builtin(argv[1])) {
        return 1;
    }

    if (find_program(argv[1], path)) {
        printf("%s\n", path);
        return 0;
    }

    return 1;
}

int run_builtin(char *argv[]) {
    if (strcmp(argv[0], "cd") == 0) return builtin_cd(argv);
    if (strcmp(argv[0], "pwd") == 0) return builtin_pwd();
    if (strcmp(argv[0], "which") == 0) return builtin_which(argv);
    if (strcmp(argv[0], "exit") == 0) return 0;
    return 1;
}