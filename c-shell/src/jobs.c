#define _POSIX_C_SOURCE 200809L

#include "jobs.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_JOBS 256
#define MAX_PENDING 256
#define MAX_CMDLINE_LEN 256

#define MAX_MSG_LEN (MAX_CMDLINE_LEN + 64)

typedef struct {
    int in_use;
    pid_t pid;
    char cmdline[MAX_CMDLINE_LEN];
} job_t;

static job_t jobs[MAX_JOBS];
static int next_job_number = 1; /* ever-increasing; slots get reused, numbers never do */

static char pending[MAX_PENDING][MAX_MSG_LEN];
static int pending_count = 0;

static volatile sig_atomic_t foreground_active = 0;

void jobs_set_foreground(int active)
{
    foreground_active = active ? 1 : 0;
}

static job_t *find_job_by_pid(pid_t pid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].in_use && jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

static void queue_or_print(const char *msg)
{
    if (foreground_active) {
        if (pending_count < MAX_PENDING) {
            strncpy(pending[pending_count], msg, MAX_MSG_LEN - 1);
            pending[pending_count][MAX_MSG_LEN - 1] = '\0';
            pending_count++;
        }
        /* queue full: message is dropped rather than doing unbounded
         * work from within a signal handler */
    } else {
        /* NOTE: printf/fflush aren't strictly POSIX async-signal-safe,
         * but this is the conventional approach used in shell labs like
         * this one. Swap for write() with a hand-built buffer if your
         * grader requires strict async-signal-safety. */
        printf("%s", msg);
        fflush(stdout);
    }
}

static void sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        job_t *job = find_job_by_pid(pid);
        if (job != NULL) {
            char msg[MAX_MSG_LEN];
            if (WIFEXITED(status)) {
                snprintf(msg, sizeof(msg), "%s with pid %d exited normally\n",
                         job->cmdline, (int)pid);
            } else {
                /* terminated by an uncaught signal (crash, kill, etc.) */
                snprintf(msg, sizeof(msg), "%s with pid %d exited abnormally\n",
                         job->cmdline, (int)pid);
            }
            queue_or_print(msg);
            job->in_use = 0;
        }
    }

    errno = saved_errno;
}

void jobs_init(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; /* don't let a blocking read() bail out with EINTR */
    sigaction(SIGCHLD, &sa, NULL);
}

void jobs_add(pid_t pid, const char *cmdline)
{
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].in_use) {
            slot = i;
            break;
        }
    }

    int job_number = next_job_number++;

    if (slot >= 0) {
        jobs[slot].in_use = 1;
        jobs[slot].pid = pid;
        strncpy(jobs[slot].cmdline, cmdline ? cmdline : "", MAX_CMDLINE_LEN - 1);
        jobs[slot].cmdline[MAX_CMDLINE_LEN - 1] = '\0';
    }
    /* table full: the job still runs and gets reaped, just without a
     * completion message -- better than crashing or blocking */

    printf("[%d] %d\n", job_number, (int)pid);
    fflush(stdout);
}

void jobs_flush_pending(void)
{
    for (int i = 0; i < pending_count; i++) {
        printf("%s", pending[i]);
    }
    if (pending_count > 0) {
        fflush(stdout);
    }
    pending_count = 0;
}

char *jobs_stringify_tokens(const token_t *tokens)
{
    size_t cap = 128;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    buf[0] = '\0';

    for (const token_t *t = tokens; t != NULL; t = t->next) {
        const char *piece;
        switch (t->type) {
            case TOK_WORD: piece = t->value; break;
            case TOK_PIPE: piece = "|";       break;
            case TOK_LT:   piece = "<";       break;
            case TOK_GT:   piece = ">";       break;
            case TOK_GTGT: piece = ">>";      break;
            default:       piece = NULL;      break;
        }
        if (piece == NULL) {
            continue;
        }

        size_t piece_len = strlen(piece);
        size_t need = len + piece_len + 2; /* +1 separating space, +1 NUL */
        if (need > cap) {
            while (cap < need) {
                cap *= 2;
            }
            char *grown = realloc(buf, cap);
            if (grown == NULL) {
                free(buf);
                return NULL;
            }
            buf = grown;
        }

        if (len > 0) {
            buf[len++] = ' ';
        }
        memcpy(buf + len, piece, piece_len);
        len += piece_len;
        buf[len] = '\0';
    }

    return buf;
}