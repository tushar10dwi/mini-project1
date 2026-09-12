/* Must come before any system header: without it, strict ISO C builds
 * won't expose readlink()/opendir() etc. from their headers. */
#define _POSIX_C_SOURCE 200809L

#include "spy.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_PATH_LEN 4096
#define MAX_MEM_FILES 256
#define MAX_FDS 4096

/* req 5: TYPE must correctly identify REG/DIR/CHR/BLK/etc. req 5's
 * note that network-specific (socket) objects are out of scope means
 * we don't need to classify those -- simplest is to just not print
 * anything we can't cleanly place in that set. */
static const char *type_str(mode_t mode)
{
    if (S_ISREG(mode))  return "REG";
    if (S_ISDIR(mode))  return "DIR";
    if (S_ISCHR(mode))  return "CHR";
    if (S_ISBLK(mode))  return "BLK";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISLNK(mode))  return "LNK";
    return NULL; /* SOCK or unknown -- out of scope / skip */
}

static int readlink_full(const char *link_path, char *out, size_t out_size)
{
    ssize_t n = readlink(link_path, out, out_size - 1);
    if (n < 0) {
        return 0;
    }
    out[n] = '\0';
    return 1;
}

/* req 3/4/6: one PID/FD/TYPE/PATH row. Silently skips paths we can't
 * stat (already-closed/dangling target) or whose type is out of
 * scope (sockets). */
static void print_entry(pid_t pid, const char *fd_label, const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }
    const char *t = type_str(st.st_mode);
    if (t == NULL) {
        return;
    }
    printf("%-8d%-8s%-8s%s\n", (int)pid, fd_label, t, path);
}

static int cmp_int(const void *a, const void *b)
{
    return (*(const int *)a) - (*(const int *)b);
}

void spy_execute(int argc, char **argv)
{
    if (argc > 2) {
        printf("spy: invalid syntax\n"); /* req 8 */
        return;
    }

    pid_t pid;
    if (argc == 1) {
        pid = getpid(); /* req 1: no pid -> the shell itself */
    } else {
        const char *s = argv[1];
        if (*s == '\0') {
            printf("spy: invalid syntax\n");
            return;
        }
        for (const char *c = s; *c != '\0'; c++) {
            if (*c < '0' || *c > '9') {
                printf("spy: invalid syntax\n");
                return;
            }
        }
        pid = (pid_t)atoi(s); /* req 2 */
    }

    char proc_dir[64];
    snprintf(proc_dir, sizeof(proc_dir), "/proc/%d", (int)pid);
    struct stat proc_st;
    if (stat(proc_dir, &proc_st) != 0) {
        printf("spy: no such process\n"); /* req 7 */
        return;
    }

    printf("%-8s%-8s%-8s%s\n", "PID", "FD", "TYPE", "PATH");

    char link_path[128];
    char target[MAX_PATH_LEN];

    /* cwd */
    snprintf(link_path, sizeof(link_path), "/proc/%d/cwd", (int)pid);
    if (readlink_full(link_path, target, sizeof(target))) {
        print_entry(pid, "cwd", target);
    }

    /* txt: the executable itself */
    snprintf(link_path, sizeof(link_path), "/proc/%d/exe", (int)pid);
    if (readlink_full(link_path, target, sizeof(target))) {
        print_entry(pid, "txt", target);
    }

    /* mem: unique file-backed mappings out of /proc/pid/maps. Each
     * line's last whitespace-separated field is the mapped file, if
     * the region is file-backed at all -- anonymous regions (heap,
     * stack, plain anon mmaps) have no such field, and pseudo-paths
     * like [heap]/[stack]/[vdso] don't start with '/', so both are
     * naturally skipped by the absolute-path check below. */
    snprintf(link_path, sizeof(link_path), "/proc/%d/maps", (int)pid);
    FILE *maps = fopen(link_path, "r");
    if (maps != NULL) {
        static char seen[MAX_MEM_FILES][MAX_PATH_LEN];
        int seen_count = 0;
        char line[MAX_PATH_LEN + 256];

        while (fgets(line, sizeof(line), maps) != NULL) {
            char *nl = strchr(line, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }
            char *last_space = strrchr(line, ' ');
            if (last_space == NULL) {
                continue;
            }
            char *path = last_space + 1;
            if (path[0] != '/') {
                continue;
            }

            int dup = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], path) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                continue; /* req 4: each unique mem file printed once */
            }
            if (seen_count < MAX_MEM_FILES) {
                strncpy(seen[seen_count], path, MAX_PATH_LEN - 1);
                seen[seen_count][MAX_PATH_LEN - 1] = '\0';
                seen_count++;
            }
            print_entry(pid, "mem", path);
        }
        fclose(maps);
    }

    /* numeric fds: 0, 1, 2, and anything else the process has open */
    snprintf(link_path, sizeof(link_path), "/proc/%d/fd", (int)pid);
    DIR *fd_dir = opendir(link_path);
    if (fd_dir != NULL) {
        int fds[MAX_FDS];
        int fd_count = 0;
        struct dirent *entry;

        while ((entry = readdir(fd_dir)) != NULL) {
            if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
                continue; /* skip "." / ".." */
            }
            if (fd_count < MAX_FDS) {
                fds[fd_count++] = atoi(entry->d_name);
            }
        }
        closedir(fd_dir);

        qsort(fds, fd_count, sizeof(int), cmp_int);

        for (int i = 0; i < fd_count; i++) {
            char fd_link[160];
            char fd_label[16];
            snprintf(fd_link, sizeof(fd_link), "/proc/%d/fd/%d", (int)pid, fds[i]);
            snprintf(fd_label, sizeof(fd_label), "%d", fds[i]);
            if (readlink_full(fd_link, target, sizeof(target))) {
                print_entry(pid, fd_label, target);
            }
        }
    }

    fflush(stdout);
}