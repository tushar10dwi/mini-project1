#define _POSIX_C_SOURCE 200809L 
#include "peek.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PEEK_CHUNK 4096      
#define MAX_PEEK_FILES 256

typedef struct {
    char *text;  
    int number;   
} line_t;

typedef struct {
    line_t *items;
    size_t count;
    size_t cap;
} line_list_t;

static void line_list_push(line_list_t *list, const char *text, size_t len)
{
    if (list->count == list->cap) {
        size_t new_cap = list->cap == 0 ? 64 : list->cap * 2;
        line_t *grown = realloc(list->items, new_cap * sizeof(line_t));
        if (grown == NULL) {
            return;
        }
        list->items = grown;
        list->cap = new_cap;
    }

    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';

    list->items[list->count].text = copy;
    list->items[list->count].number = -1;
    list->count++;
}

static void line_list_free(line_list_t *list)
{
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].text);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}
static void print_no_such_file(void)
{
    printf("peek: no such file or directory\n");
}

static void print_is_a_directory(void)
{
    printf("peek: is a directory\n");
}

static void read_reverse_seekable(int fd, off_t filesize, line_list_t *out)
{
    off_t pos = filesize;
    char *pending = NULL;
    size_t pending_len = 0;
    int first_extraction = 1;
    char chunk[PEEK_CHUNK];

    while (pos > 0) {
        size_t want = (pos >= (off_t)PEEK_CHUNK) ? PEEK_CHUNK : (size_t)pos;
        pos -= (off_t)want;

        if (lseek(fd, pos, SEEK_SET) == (off_t)-1) {
            break;
        }
        ssize_t got = read(fd, chunk, want);
        if (got <= 0) {
            break;
        }

        char *grown = malloc((size_t)got + pending_len);
        if (grown == NULL) {
            break;
        }
        memcpy(grown, chunk, (size_t)got);
        if (pending_len > 0) {
            memcpy(grown + got, pending, pending_len);
        }
        free(pending);
        pending = grown;
        pending_len += (size_t)got;

        for (;;) {
            ssize_t i = (ssize_t)pending_len - 1;
            while (i >= 0 && pending[i] != '\n') {
                i--;
            }
            if (i < 0) {
                break; 
            }

            size_t line_start = (size_t)i + 1;
            size_t line_len = pending_len - line_start;

            if (!(first_extraction && line_len == 0)) {
                line_list_push(out, pending + line_start, line_len);
            }
            first_extraction = 0;
            pending_len = (size_t)i; 
        }
    }

    if (pending_len > 0 || !first_extraction) {
        line_list_push(out, pending, pending_len);
    }
    free(pending);
}

static void read_reverse_buffered(int fd, line_list_t *out)
{
    char chunk[PEEK_CHUNK];
    char *buf = NULL;
    size_t len = 0, cap = 0;
    ssize_t got;

    while ((got = read(fd, chunk, sizeof(chunk))) > 0) {
        if (len + (size_t)got > cap) {
            size_t new_cap = (len + (size_t)got) * 2;
            char *grown = realloc(buf, new_cap);
            if (grown == NULL) {
                break;
            }
            buf = grown;
            cap = new_cap;
        }
        memcpy(buf + len, chunk, (size_t)got);
        len += (size_t)got;
    }

    line_list_t natural = {0};
    size_t line_start = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            line_list_push(&natural, buf + line_start, i - line_start);
            line_start = i + 1;
        }
    }
    if (line_start < len) {
        line_list_push(&natural, buf + line_start, len - line_start);
    }
    free(buf);

    for (ssize_t i = (ssize_t)natural.count - 1; i >= 0; i--) {
        line_list_push(out, natural.items[i].text, strlen(natural.items[i].text));
    }
    line_list_free(&natural);
}

static void assign_numbers(line_list_t *rev, int *counter)
{
    for (ssize_t k = (ssize_t)rev->count - 1; k >= 0; k--) {
        if (rev->items[k].text[0] != '\0') {
            (*counter)++;
            rev->items[k].number = *counter;
        } else {
            rev->items[k].number = -1;
        }
    }
}

static void print_reverse(const line_list_t *rev, int show_numbers)
{
    for (size_t k = 0; k < rev->count; k++) {
        if (rev->items[k].number == -1) {
            printf("\n"); /* empty lines never get a number */
        } else if (show_numbers) {
            printf("%d %s\n", rev->items[k].number, rev->items[k].text);
        } else {
            printf("%s\n", rev->items[k].text);
        }
    }
}

static void process_forward(FILE *fp, int show_numbers, int *counter)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&line, &cap, fp)) != -1) {
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        if (len == 0) {
            printf("\n");
        } else if (show_numbers) {
            (*counter)++;
            printf("%d %s\n", *counter, line);
        } else {
            printf("%s\n", line);
        }
    }

    free(line);
}

static int process_source(const char *name, int show_numbers, int reverse_flag, int *counter)
{
    int use_stdin = (name == NULL || strcmp(name, "-") == 0);
    int fd;

    if (use_stdin) {
        fd = STDIN_FILENO;
    } else {
        fd = open(name, O_RDONLY);
        if (fd < 0) {
            print_no_such_file();
            return -1;
        }
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        print_is_a_directory();
        if (!use_stdin) {
            close(fd);
        }
        return -1;
    }

    int seekable = !use_stdin && S_ISREG(st.st_mode) && lseek(fd, 0, SEEK_CUR) != (off_t)-1;

    if (reverse_flag) {
        line_list_t rev = {0};
        if (seekable) {
            read_reverse_seekable(fd, st.st_size, &rev);
        } else {
            read_reverse_buffered(fd, &rev);
        }
        assign_numbers(&rev, counter);
        print_reverse(&rev, show_numbers);
        line_list_free(&rev);
        if (!use_stdin) {
            close(fd);
        }
    } else {
        FILE *fp = use_stdin ? stdin : fdopen(fd, "r");
        if (fp == NULL) {
            print_no_such_file();
            if (!use_stdin) {
                close(fd);
            }
            return -1;
        }
        process_forward(fp, show_numbers, counter);
        if (!use_stdin) {
            fclose(fp); 
        }
    }

    return 0;
}

int peek_execute(int argc, char **argv)
{
    int show_numbers = 0;
    int reverse_flag = 0;
    const char *files[MAX_PEEK_FILES];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *tok = argv[i];

        if (tok[0] == '-' && tok[1] != '\0') {
            for (int j = 1; tok[j] != '\0'; j++) {
                if (tok[j] == 'n') {
                    show_numbers = 1;
                } else if (tok[j] == 'r') {
                    reverse_flag = 1;
                }
            }
        } else {
            if (file_count < MAX_PEEK_FILES) {
                files[file_count++] = tok;
            }
        }
    }

    int counter = 0;
    int any_error = 0;

    if (file_count == 0) {
        if (process_source(NULL, show_numbers, reverse_flag, &counter) != 0) {
            any_error = 1;
        }
    } else {
        for (int i = 0; i < file_count; i++) {
            if (process_source(files[i], show_numbers, reverse_flag, &counter) != 0) {
                any_error = 1;
            }
        }
    }

    return any_error ? -1 : 0;
}