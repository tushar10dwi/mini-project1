#include "prompt.h"
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* The directory the shell was started in. Set once by prompt_init(). */
static char home_dir[PATH_MAX];

void prompt_init(void)
{
    if (getcwd(home_dir, sizeof(home_dir)) == NULL) {
        perror("prompt_init: getcwd");
        exit(EXIT_FAILURE);
    }
}

const char *prompt_home_dir(void)
{
    return home_dir;
}

/*
 * Writes the display path for the current working directory into out
 * (a buffer of size out_size), substituting the shell's home
 * directory prefix with '~' when applicable.
 */
static void get_display_path(char *out, size_t out_size)
{
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        /* Fall back to something sane rather than crashing the prompt. */
        snprintf(out, out_size, "?");
        return;
    }

    size_t home_len = strlen(home_dir);

    if (strcmp(cwd, home_dir) == 0) {
        /* cwd is exactly the home directory. */
        snprintf(out, out_size, "~");
        return;
    }

    if (strncmp(cwd, home_dir, home_len) == 0 && cwd[home_len] == '/') {
        /* home_dir is a proper ancestor of cwd: replace the prefix. */
        snprintf(out, out_size, "~%s", cwd + home_len);
        return;
    }

    /* home_dir is not an ancestor: show the absolute path as-is. */
    snprintf(out, out_size, "%s", cwd);
}

static const char *get_username(void)
{
    struct passwd *pw = getpwuid(geteuid());
    if (pw != NULL && pw->pw_name != NULL) {
        return pw->pw_name;
    }
    return "unknown";
}

static void get_host(char *out, size_t out_size)
{
    if (gethostname(out, out_size) != 0) {
        snprintf(out, out_size, "unknown");
    }
}

void prompt_print(void)
{
    char host[HOST_NAME_MAX + 1];
    char path[PATH_MAX];

    get_host(host, sizeof(host));
    get_display_path(path, sizeof(path));

    printf("<%s@%s:%s> ", get_username(), host, path);
    fflush(stdout);
}