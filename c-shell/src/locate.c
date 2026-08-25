#include "locate.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static int build_absolute(const char *dir, const char *filename, char *out, size_t out_size)
{
    char resolved_dir[PATH_MAX];
    if (realpath(dir, resolved_dir) == NULL) {
        return 0; 
    }
    snprintf(out, out_size, "%s/%s", resolved_dir, filename);
    return 1;
}

static int check_and_print(const char *dir, const char *filename)
{
    char abspath[PATH_MAX + 256];
    if (!build_absolute(dir, filename, abspath, sizeof(abspath))) {
        return 0;
    }
    if (is_executable_file(abspath)) {
        printf("%s\n", abspath);
        return 1;
    }
    return 0;
}

static int locate_one(const char *filename)
{
    int found_any = 0;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        char abspath[PATH_MAX + 256];
        snprintf(abspath, sizeof(abspath), "%s/%s", cwd, filename);
        if (is_executable_file(abspath)) {
            printf("%s\n", abspath);
            found_any = 1;
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
                if (check_and_print(dir, filename)) {
                    found_any = 1;
                }

                if (colon == NULL) {
                    break;
                }
                seg = colon + 1;
            }
            free(copy);
        }
    }

    if (!found_any) {
        printf("locate: command not found (%s)\n", filename);
    }

    return found_any ? 0 : -1;
}

int locate_execute(int argc, char **argv)
{
    if (argc <= 1) {
        printf("locate: invalid syntax\n");
        return -1;
    }

    int any_error = 0;
    for (int i = 1; i < argc; i++) {
        if (locate_one(argv[i]) != 0) {
            any_error = 1;
        }
    }

    return any_error ? -1 : 0;
}