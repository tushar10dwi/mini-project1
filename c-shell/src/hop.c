#include "hop.h"
#include "prompt.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* --- Frecency record store ------------------------------------------- */

typedef struct {
    char path[PATH_MAX];
    double rank;
    time_t last_access;
} hop_entry_t;

static hop_entry_t *entries = NULL;
static size_t entry_count = 0;
static size_t entry_cap = 0;
static int store_dirty = 0;

/*
 * The CWD hop changed *from* on the most recently successful directory
 * change, used to implement "hop -". Empty until the first hop.
 */
static char prev_cwd[PATH_MAX] = "";

static void store_file_path(char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        home = prompt_home_dir();
    }
    snprintf(out, out_size, "%s/.cshell_hop_frecency", home);
}

static int ensure_capacity(void)
{
    if (entry_count < entry_cap) {
        return 1;
    }

    size_t new_cap = entry_cap == 0 ? 16 : entry_cap * 2;
    hop_entry_t *grown = realloc(entries, new_cap * sizeof(hop_entry_t));
    if (grown == NULL) {
        return 0;
    }

    entries = grown;
    entry_cap = new_cap;
    return 1;
}

void hop_init(void)
{
    char path[PATH_MAX];
    store_file_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return; /* No store on disk yet: start empty. */
    }

    char line[PATH_MAX + 64];
    while (fgets(line, sizeof(line), f) != NULL) {
        double rank;
        long last_access;
        char dirpath[PATH_MAX];

        if (sscanf(line, "%lf %ld %4095[^\n]", &rank, &last_access, dirpath) != 3) {
            continue; /* Skip malformed lines rather than aborting. */
        }

        if (!ensure_capacity()) {
            break;
        }

        snprintf(entries[entry_count].path, sizeof(entries[entry_count].path), "%s", dirpath);
        entries[entry_count].rank = rank;
        entries[entry_count].last_access = (time_t)last_access;
        entry_count++;
    }

    fclose(f);
}

void hop_save(void)
{
    if (!store_dirty) {
        return;
    }

    char path[PATH_MAX];
    store_file_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return;
    }

    for (size_t i = 0; i < entry_count; i++) {
        fprintf(f, "%f %ld %s\n", entries[i].rank, (long)entries[i].last_access, entries[i].path);
    }

    fclose(f);
}

static hop_entry_t *find_entry(const char *canon_path)
{
    for (size_t i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].path, canon_path) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

/* Records a successful hop into an existing directory `dir` for
 * frecency purposes. `dir` is canonicalized so the same directory
 * reached via different relative paths shares one record. */
static void record_visit(const char *dir)
{
    char canon[PATH_MAX];
    if (realpath(dir, canon) == NULL) {
        return; /* Shouldn't happen: we just chdir'd there. */
    }

    hop_entry_t *e = find_entry(canon);
    if (e != NULL) {
        e->rank += 1.0;
        e->last_access = time(NULL);
        store_dirty = 1;
        return;
    }

    if (!ensure_capacity()) {
        return;
    }

    snprintf(entries[entry_count].path, sizeof(entries[entry_count].path), "%s", canon);
    entries[entry_count].rank = 1.0;
    entries[entry_count].last_access = time(NULL);
    entry_count++;
    store_dirty = 1;
}

/* --- Scoring: rank * recency weight, zoxide-style aging buckets ------- */

static double recency_weight(time_t last_access)
{
    double age = difftime(time(NULL), last_access);

    if (age < 3600.0) {          /* < 1 hour */
        return 4.0;
    } else if (age < 86400.0) {  /* < 1 day */
        return 2.0;
    } else if (age < 604800.0) { /* < 1 week */
        return 0.5;
    }
    return 0.25;
}

static double score_of(const hop_entry_t *e)
{
    return e->rank * recency_weight(e->last_access);
}

static int dir_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

/*
 * Finds the highest-scoring stored directory whose path contains
 * `name` as a substring and that still exists on disk (lower-scoring
 * matches that no longer exist are skipped). Returns 1 and fills out
 * (size out_size) on success, 0 if there is no usable match.
 */
static int frecency_lookup(const char *name, char *out, size_t out_size)
{
    if (entry_count == 0) {
        return 0;
    }

    size_t *candidates = malloc(entry_count * sizeof(size_t));
    if (candidates == NULL) {
        return 0;
    }

    size_t cand_count = 0;
    for (size_t i = 0; i < entry_count; i++) {
        if (strstr(entries[i].path, name) != NULL) {
            candidates[cand_count++] = i;
        }
    }

    int found = 0;
    while (cand_count > 0) {
        size_t best = 0;
        for (size_t i = 1; i < cand_count; i++) {
            if (score_of(&entries[candidates[i]]) > score_of(&entries[candidates[best]])) {
                best = i;
            }
        }

        if (dir_exists(entries[candidates[best]].path)) {
            snprintf(out, out_size, "%s", entries[candidates[best]].path);
            found = 1;
            break;
        }

        /* Best candidate no longer exists on disk: drop it, try next. */
        candidates[best] = candidates[cand_count - 1];
        cand_count--;
    }

    free(candidates);
    return found;
}

/* --- Per-argument resolution ------------------------------------------ */

static void print_no_such_directory(void)
{
    printf("hop: no such directory\n");
}

/*
 * Resolves and applies a single hop argument. Returns 1 on success
 * (including no-op cases like "." or a rootless ".."), 0 on failure
 * (error already printed).
 */
static int hop_one(const char *arg)
{
    char cwd_before[PATH_MAX];
    if (getcwd(cwd_before, sizeof(cwd_before)) == NULL) {
        cwd_before[0] = '\0';
    }

    if (strcmp(arg, "~") == 0) {
        if (chdir(prompt_home_dir()) != 0) {
            print_no_such_directory();
            return 0;
        }
    } else if (strcmp(arg, ".") == 0) {
        return 1; /* Stay put: nothing to record. */
    } else if (strcmp(arg, "..") == 0) {
        if (strcmp(cwd_before, "/") == 0) {
            return 1; /* No parent of root: no-op per spec. */
        }
        if (chdir("..") != 0) {
            return 1; /* No parent reachable: treat as no-op per spec. */
        }
    } else if (strcmp(arg, "-") == 0) {
        if (prev_cwd[0] == '\0') {
            return 1; /* No previous CWD yet: no-op per spec. */
        }
        if (chdir(prev_cwd) != 0) {
            print_no_such_directory();
            return 0;
        }
    } else if (dir_exists(arg)) {
        if (chdir(arg) != 0) {
            print_no_such_directory();
            return 0;
        }
    } else {
        char match[PATH_MAX];
        if (!frecency_lookup(arg, match, sizeof(match))) {
            print_no_such_directory();
            return 0;
        }
        if (chdir(match) != 0) {
            print_no_such_directory();
            return 0;
        }
    }

    char cwd_after[PATH_MAX];
    if (getcwd(cwd_after, sizeof(cwd_after)) != NULL) {
        if (cwd_before[0] != '\0' && strcmp(cwd_before, cwd_after) != 0) {
            snprintf(prev_cwd, sizeof(prev_cwd), "%s", cwd_before);
        }
        record_visit(cwd_after);
    }

    return 1;
}

int hop_execute(int argc, char **argv)
{
    if (argc <= 1) {
        return hop_one("~") ? 0 : -1;
    }

    for (int i = 1; i < argc; i++) {
        if (!hop_one(argv[i])) {
            return -1;
        }
    }

    return 0;
}

const char *hop_get_prev_dir(void)
{
    return prev_cwd;
}