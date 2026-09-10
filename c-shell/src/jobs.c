/* Must come before any system header: without it, strict ISO C builds
 * (e.g. -std=c11/c23 without GNU extensions) won't expose struct
 * sigaction, SA_RESTART, tcsetpgrp(), or setpgid() from their headers. */
#define _POSIX_C_SOURCE 200809L

#include "jobs.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* NOTE: assumes token.h's TOK_WORD / TOK_PIPE / TOK_LT / TOK_GT / TOK_GTGT
 * names, matching the rest of the codebase. */

#define MAX_JOBS 256
#define MAX_PROCS_PER_JOB 64   /* matches pipeline.c's MAX_CMDS */
#define MAX_PENDING 256
#define MAX_NAME_LEN 64
#define MAX_CMDLINE_LEN 256
/* Headroom above MAX_CMDLINE_LEN for the literal text around it (e.g.
 * " with pid " / " exited abnormally\n") plus a worst-case pid (up to
 * 11 digits). Without this margin, gcc's -Wformat-truncation can't
 * prove the snprintf() below never truncates and refuses to build
 * under -Werror. */
#define MAX_MSG_LEN (MAX_CMDLINE_LEN + 64)

typedef enum { PROC_RUNNING, PROC_STOPPED } proc_state_t;

typedef struct {
    int in_use;            /* still alive (not yet reaped as exited) */
    pid_t pid;
    char name[MAX_NAME_LEN];
    proc_state_t state;
} proc_t;

typedef struct {
    int in_use;             /* has at least one live process left */
    int job_number;
    pid_t pgid;              /* == procs[0].pid, by convention */
    char cmdline[MAX_CMDLINE_LEN];
    proc_t procs[MAX_PROCS_PER_JOB];
    int num_procs;
} job_t;

/* Append-only: job slots are never reused, so array order always
 * matches launch order (requirement E1.6 -- oldest first) without
 * needing to sort by job_number at print time. */
static job_t jobs[MAX_JOBS];
static int num_job_slots = 0;
static int next_job_number = 1; /* ever-increasing; never reused (D2.4) */

/* Messages queued while a foreground command is running (D2.11). */
static char pending[MAX_PENDING][MAX_MSG_LEN];
static int pending_count = 0;

static volatile sig_atomic_t foreground_active = 0;

static pid_t shell_pgid = 0;

pid_t jobs_get_shell_pgid(void)
{
    return shell_pgid;
}

void jobs_set_foreground(int active)
{
    foreground_active = active ? 1 : 0;
}

static int find_proc_by_pid(pid_t pid, job_t **out_job, proc_t **out_proc)
{
    for (int i = 0; i < num_job_slots; i++) {
        job_t *job = &jobs[i];
        if (!job->in_use) {
            continue;
        }
        for (int p = 0; p < job->num_procs; p++) {
            if (job->procs[p].in_use && job->procs[p].pid == pid) {
                *out_job = job;
                *out_proc = &job->procs[p];
                return 1;
            }
        }
    }
    return 0;
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

/* Marks a whole job as no longer having any live processes if that's
 * now true, so jobs_print_activities() can skip it cheaply. */
static void update_job_liveness(job_t *job)
{
    for (int p = 0; p < job->num_procs; p++) {
        if (job->procs[p].in_use) {
            return;
        }
    }
    job->in_use = 0;
}

static void sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    int status;
    pid_t pid;

    /* WNOHANG: never block the shell while reaping (D2.7).
     * WUNTRACED: also notice a process being stopped (e.g. by SIGTTIN
     * when a background group tries to read the terminal), not just
     * terminated, so `activities` can report it as Stopped. */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job_t *job = NULL;
        proc_t *proc = NULL;
        if (!find_proc_by_pid(pid, &job, &proc)) {
            continue; /* not one of our tracked background processes */
        }

        if (WIFSTOPPED(status)) {
            proc->state = PROC_STOPPED;
            continue; /* still alive, just stopped -- no message (E1 only) */
        }

        /* terminated: exited or killed by a signal */
        proc->in_use = 0;
        update_job_liveness(job);

        if (pid == job->pgid) {
            /* the group leader / first command in the pipeline is the
             * one whose completion gets reported (D2.13) */
            char msg[MAX_MSG_LEN];
            if (WIFEXITED(status)) {
                snprintf(msg, sizeof(msg), "%s with pid %d exited normally\n",
                         job->cmdline, (int)pid);
            } else {
                snprintf(msg, sizeof(msg), "%s with pid %d exited abnormally\n",
                         job->cmdline, (int)pid);
            }
            queue_or_print(msg);
        }
        /* other pipeline stages are reaped silently, with no message */
    }

    errno = saved_errno;
}

void jobs_init(void)
{
    /* Put the shell in its own process group (usually already true for
     * an interactively-started shell, but harmless/idempotent if so;
     * EPERM here typically just means it already is). */
    if (setpgid(0, 0) < 0 && errno != EPERM) {
        perror("cshell: setpgid");
    }
    shell_pgid = getpid();

    /* Take control of the terminal for the shell's own group. */
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    /* Ignore SIGTTOU/SIGTTIN in the shell itself: otherwise, later
     * calls to tcsetpgrp() (e.g. reclaiming the terminal right after a
     * foreground job exits, when the shell is momentarily not the
     * terminal's foreground group) would stop the shell itself. */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; /* don't let a blocking read() bail out with EINTR */
    sigaction(SIGCHLD, &sa, NULL);
}

void jobs_add(const pid_t *pids, char *const *names, int count, const char *cmdline)
{
    if (count > MAX_PROCS_PER_JOB) {
        count = MAX_PROCS_PER_JOB; /* defensive; pipeline.c already caps this */
    }

    int job_number = next_job_number++;

    if (num_job_slots < MAX_JOBS) {
        job_t *job = &jobs[num_job_slots++];
        job->in_use = 1;
        job->job_number = job_number;
        job->pgid = pids[0];
        job->num_procs = count;
        strncpy(job->cmdline, cmdline ? cmdline : "", MAX_CMDLINE_LEN - 1);
        job->cmdline[MAX_CMDLINE_LEN - 1] = '\0';

        for (int i = 0; i < count; i++) {
            job->procs[i].in_use = 1;
            job->procs[i].pid = pids[i];
            job->procs[i].state = PROC_RUNNING;
            const char *n = (names != NULL && names[i] != NULL) ? names[i] : "";
            strncpy(job->procs[i].name, n, MAX_NAME_LEN - 1);
            job->procs[i].name[MAX_NAME_LEN - 1] = '\0';
        }
    }
    /* table full: the job still runs and gets reaped, just without
     * activities/completion tracking -- better than crashing */

    printf("[%d] %d\n", job_number, (int)pids[0]);
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

void jobs_print_activities(void)
{
    for (int i = 0; i < num_job_slots; i++) {
        job_t *job = &jobs[i];
        if (!job->in_use) {
            continue;
        }

        int any_live = 0;
        for (int p = 0; p < job->num_procs; p++) {
            if (job->procs[p].in_use) {
                any_live = 1;
                break;
            }
        }
        if (!any_live) {
            continue;
        }

        printf("[%d] pgid %d\n", job->job_number, (int)job->pgid);
        for (int p = 0; p < job->num_procs; p++) {
            if (!job->procs[p].in_use) {
                continue; /* exited: not shown (E1.7) */
            }
            printf("  %d %s %s\n",
                   (int)job->procs[p].pid,
                   job->procs[p].name,
                   job->procs[p].state == PROC_STOPPED ? "Stopped" : "Running");
        }
    }
    fflush(stdout);
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