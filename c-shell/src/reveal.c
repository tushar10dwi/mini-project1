#include "reveal.h"
#include "hop.h"
#include "prompt.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_REVEAL_ENTRIES 4096

static void print_invalid_syntax(void)
{
    printf("reveal: invalid syntax\n");
}

static void print_no_such_directory(void)
{
    printf("reveal: no such directory\n");
}

static int dir_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int resolve_target(const char *arg, char *out, size_t out_size)
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return 0;
    }

    if (arg == NULL) {
        snprintf(out, out_size, "%s", cwd);
        return 1;
    }
    if (strcmp(arg, "~") == 0) {
        snprintf(out, out_size, "%s", prompt_home_dir());
        return 1;
    }
    if (strcmp(arg, ".") == 0) {
        snprintf(out, out_size, "%s", cwd);
        return 1;
    }
    if (strcmp(arg, "..") == 0) {
        snprintf(out, out_size, "%s/..", cwd);
        return 1;
    }
    if (strcmp(arg, "-") == 0) {
        const char *prev = hop_get_prev_dir();
        if (prev[0] == '\0') {
            return 0;
        }
        snprintf(out, out_size, "%s", prev);
        return 1;
    }

    snprintf(out, out_size, "%s", arg);
    return 1;
}

static int cmp_ascii(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int list_dir_sorted(const char *path, int show_all, char *names[], int max)
{
    DIR *d = opendir(path);
    if (d == NULL) {
        return -1;
    }

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!show_all && ent->d_name[0] == '.') {
            continue;
        }
        names[n] = strdup(ent->d_name);
        if (names[n] != NULL) {
            n++;
        }
    }
    closedir(d);

    qsort(names, n, sizeof(char *), cmp_ascii);
    return n;
}

static int is_subdir(const char *base, const char *name)
{
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", base, name);
    return dir_exists(full);
}

static void print_flat(const char *path, int show_all)
{
    char *names[MAX_REVEAL_ENTRIES];
    int n = list_dir_sorted(path, show_all, names, MAX_REVEAL_ENTRIES);
    if (n < 0) {
        print_no_such_directory();
        return;
    }
    for (int i = 0; i < n; i++) {
        printf("%s\n", names[i]);
        free(names[i]);
    }
}

static void print_recursive(const char *path, int show_all)
{
    char *names[MAX_REVEAL_ENTRIES];
    int n = list_dir_sorted(path, show_all, names, MAX_REVEAL_ENTRIES);
    if (n < 0) {
        print_no_such_directory();
        return;
    }

    for (int i = 0; i < n; i++) {
        if (is_subdir(path, names[i])) {
            printf("%s/\n", names[i]);
        } else {
            printf("%s\n", names[i]);
        }
    }

    for (int i = 0; i < n; i++) {
        if (is_subdir(path, names[i])) {
            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, names[i]);
            print_recursive(child, show_all);
        }
    }

    for (int i = 0; i < n; i++) {
        free(names[i]);
    }
}

int reveal_execute(int argc, char **argv)
{
    int flag_a = 0;
    int flag_t = 0;
    int bad_flag = 0;
    const char *path_arg = NULL;
    int path_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *tok = argv[i];

        if (tok[0] == '-' && tok[1] != '\0') {
            for (int j = 1; tok[j] != '\0'; j++) {
                if (tok[j] == 'a') {
                    flag_a = 1;
                } else if (tok[j] == 't') {
                    flag_t = 1;
                } else {
                    bad_flag = 1;
                }
            }
        } else {
            path_count++;
            path_arg = tok;
        }
    }

    if (bad_flag) {
        print_invalid_syntax();
        return -1;
    }
    if (path_count > 1) {
        print_invalid_syntax();
        return -1;
    }

    char resolved[PATH_MAX];
    if (!resolve_target(path_arg, resolved, sizeof(resolved)) || !dir_exists(resolved)) {
        print_no_such_directory();
        return -1;
    }

    if (flag_t) {
        print_recursive(resolved, flag_a);
    } else {
        print_flat(resolved, flag_a);
    }

    return 0;
}