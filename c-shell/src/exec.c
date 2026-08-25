#include "exec.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int is_executable_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    return access(path, X_OK) == 0;
}

static int resolve_executable(const char *raw_name, char *out, size_t out_size, char **bare_name_out)
{
    int skip_cwd = 0;
    const char *name = raw_name;

    if (name[0] == '%') {
        skip_cwd = 1;
        name++;
    }
    *bare_name_out = strdup(name);

    if (strchr(name, '/') != NULL) {
        if (is_executable_file(name)) {
            snprintf(out, out_size, "%s", name);
            return 1;
        }
        return 0;
    }

    if (!skip_cwd) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            char candidate[PATH_MAX + 256];
            snprintf(candidate, sizeof(candidate), "%s/%s", cwd, name);
            if (is_executable_file(candidate)) {
                snprintf(out, out_size, "%s", candidate);
                return 1;
            }
        }
    }

    const char *path_env = getenv("PATH");
    if (path_env != NULL) {
        char *copy = strdup(path_env);
        if (copy != NULL) {
            char *seg = copy;
            for (;;) {
                char *colon = strchr(seg, ':');
                if (colon != NULL) {
                    *colon = '\0';
                }

                const char *dir = (seg[0] == '\0') ? "." : seg;
                char resolved_dir[PATH_MAX];
                if (realpath(dir, resolved_dir) != NULL) {
                    char candidate[PATH_MAX + 256];
                    snprintf(candidate, sizeof(candidate), "%s/%s", resolved_dir, name);
                    if (is_executable_file(candidate)) {
                        snprintf(out, out_size, "%s", candidate);
                        free(copy);
                        return 1;
                    }
                }

                if (colon == NULL) {
                    break;
                }
                seg = colon + 1;
            }
            free(copy);
        }
    }

    return 0;
}

int exec_command(int argc, char **argv)
{
    if (argc < 1 || argv[0] == NULL || argv[0][0] == '\0') {
        return -1;
    }

    char resolved[PATH_MAX + 256];
    char *bare_name = NULL;
    int found = resolve_executable(argv[0], resolved, sizeof(resolved), &bare_name);

    if (!found) {
        printf("cshell: command not found (%s)\n", bare_name != NULL ? bare_name : argv[0]);
        free(bare_name);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("cshell: fork");
        free(bare_name);
        return -1;
    }

    if (pid == 0) {
        char **child_argv = malloc((size_t)(argc + 1) * sizeof(char *));
        if (child_argv == NULL) {
            _exit(127);
        }
        child_argv[0] = bare_name;
        for (int i = 1; i < argc; i++) {
            child_argv[i] = argv[i];
        }
        child_argv[argc] = NULL;

        execv(resolved, child_argv);

        fprintf(stderr, "cshell: exec failed for %s: %s\n", resolved, strerror(errno));
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);

    free(bare_name);
    return 0;
}