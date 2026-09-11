#define _POSIX_C_SOURCE 200809L
#include "jobs.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


#define MAX_JOBS 256
#define MAX_PROCS_PER_JOB 64   /* matches pipeline.c's MAX_CMDS */
#define MAX_PENDING 256
#define MAX_NAME_LEN 64
#define MAX_CMDLINE_LEN 256
#define MAX_MSG_LEN (MAX_CMDLINE_LEN + 64)

typedef enum { PROC_RUNNING, PROC_STOPPED } proc_state_t;

typedef struct {
    int in_use;            
    pid_t pid;
    char name[MAX_NAME_LEN];
    proc_state_t state;
} proc_t;

typedef struct {
    int in_use;            
    int job_number;
    pid_t pgid;             
    char cmdline[MAX_CMDLINE_LEN];
    proc_t procs[MAX_PROCS_PER_JOB];
    int num_procs;
} job_t;

static job_t jobs[MAX_JOBS];
static int num_job_slots = 0;
static int next_job_number = 1; 

static char pending[MAX_PENDING][MAX_MSG_LEN];
static int pending_count = 0;

static volatile sig_atomic_t foreground_active = 0;

static pid_t shell_pgid = 0;
static pid_t foreground_pgid = 0; 

pid_t jobs_get_shell_pgid(void)
{
    return shell_pgid;
}

void jobs_give_terminal(pid_t pgid)
{
    foreground_pgid = pgid;
    tcsetpgrp(STDIN_FILENO, pgid);
}

void jobs_reclaim_terminal(void)
{
    foreground_pgid = 0;
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}

void jobs_foreground_stopped(pid_t pgid)
{
    for (int i = 0; i < num_job_slots; i++) {
        job_t *job = &jobs[i];
        if (job->in_use && job->pgid == pgid) {
            for (int p = 0; p < job->num_procs; p++) {
                if (job->procs[p].pid == pgid) {
                    job->procs[p].state = PROC_STOPPED;
                }
            }
            printf("\n[%d] + Stopped   %s\n", job->job_number, job->cmdline);
            fflush(stdout);
            break;
        }
    }
    jobs_reclaim_terminal();
}

int jobs_have_stopped(void)
{
    for (int i = 0; i < num_job_slots; i++) {
        job_t *job = &jobs[i];
        if (!job->in_use) continue;
        for (int p = 0; p < job->num_procs; p++) {
            if (job->procs[p].in_use && job->procs[p].state == PROC_STOPPED) {
                return 1;
            }
        }
    }
    return 0;
}

void jobs_hangup_all(void)
{
    for (int i = 0; i < num_job_slots; i++) {
        job_t *job = &jobs[i];
        if (job->in_use) {
            kill(-job->pgid, SIGHUP);
        }
    }
}

static volatile sig_atomic_t sigint_caught = 0;

static void sigint_handler(int signo)
{
    (void)signo;
    sigint_caught = 1;
}

int jobs_take_sigint(void)
{
    if (sigint_caught) {
        sigint_caught = 0;
        return 1;
    }
    return 0;
}

static volatile sig_atomic_t sigtstp_caught = 0;

static void sigtstp_handler(int signo)
{
    (void)signo;
    sigtstp_caught = 1;
}

int jobs_take_sigtstp(void)
{
    if (sigtstp_caught) {
        sigtstp_caught = 0;
        return 1;
    }
    return 0;
}

static volatile sig_atomic_t alarm_fired = 0;

static void sigalrm_handler(int signo)
{
    (void)signo;
    alarm_fired = 1;
}

static job_t *find_job_by_number(int job_number)
{
    for (int i = 0; i < num_job_slots; i++) {
        if (jobs[i].in_use && jobs[i].job_number == job_number) {
            return &jobs[i];
        }
    }
    return NULL;
}

static int parse_job_arg(const char *arg, int *out_num)
{
    if (arg == NULL || arg[0] != '%' || arg[1] == '\0') {
        return 0;
    }
    for (const char *q = arg + 1; *q != '\0'; q++) {
        if (*q < '0' || *q > '9') {
            return 0;
        }
    }
    *out_num = atoi(arg + 1);
    return 1;
}

static int parse_uint_arg(const char *arg, int *out_val)
{
    if (arg == NULL || *arg == '\0') {
        return 0;
    }
    for (const char *q = arg; *q != '\0'; q++) {
        if (*q < '0' || *q > '9') {
            return 0;
        }
    }
    *out_val = atoi(arg);
    return 1;
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
    } else {
        printf("%s", msg);
        fflush(stdout);
    }
}

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

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job_t *job = NULL;
        proc_t *proc = NULL;
        if (!find_proc_by_pid(pid, &job, &proc)) {
            continue; 
        }

        if (WIFSTOPPED(status)) {
            proc->state = PROC_STOPPED;
            continue; 
        }

        proc->in_use = 0;
        update_job_liveness(job);

        if (pid == job->pgid) {
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
    }

    errno = saved_errno;
}

void jobs_init(void)
{
    if (setpgid(0, 0) < 0 && errno != EPERM) {
        perror("cshell: setpgid");
    }
    shell_pgid = getpid();

    tcsetpgrp(STDIN_FILENO, shell_pgid);

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; 
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler; 
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_tstp;
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_tstp.sa_handler = sigtstp_handler; 
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = 0;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    struct sigaction sa_alrm;
    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_handler = sigalrm_handler;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = 0; 
    sigaction(SIGALRM, &sa_alrm, NULL);
}

static void jobs_register(const pid_t *pids, char *const *names, int count, const char *cmdline)
{
    if (count > MAX_PROCS_PER_JOB) {
        count = MAX_PROCS_PER_JOB;
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
}

void jobs_add(const pid_t *pids, char *const *names, int count, const char *cmdline)
{
    jobs_register(pids, names, count, cmdline);
    printf("[%d] %d\n", next_job_number - 1, (int)pids[0]);
    fflush(stdout);
}

void jobs_add_stopped(const pid_t *pids, char *const *names, int count, const char *cmdline)
{
    jobs_register(pids, names, count, cmdline);
}

void jobs_resume_execute(int argc, char **argv)
{
    int job_number;
    if (argc < 3 || !parse_job_arg(argv[1], &job_number)) {
        printf("resume: invalid syntax\n");
        return;
    }

    int is_fg;
    if (strcmp(argv[2], "fg") == 0) {
        is_fg = 1;
    } else if (strcmp(argv[2], "bg") == 0) {
        is_fg = 0;
    } else {
        printf("resume: invalid syntax\n");
        return;
    }

    int has_timeout = 0;
    int timeout_secs = 0;
    if (argc == 5) {
        if (!is_fg || strcmp(argv[3], "--timeout") != 0 ||
            !parse_uint_arg(argv[4], &timeout_secs)) {
            printf("resume: invalid syntax\n");
            return;
        }
        has_timeout = 1;
    } else if (argc != 3) {
        printf("resume: invalid syntax\n");
        return;
    }

    job_t *job = find_job_by_number(job_number);
    if (job == NULL) {
        printf("resume: no such job\n");
        return;
    }

    /* req 2 */
    kill(-job->pgid, SIGCONT);
    for (int p = 0; p < job->num_procs; p++) {
        if (job->procs[p].in_use) {
            job->procs[p].state = PROC_RUNNING;
        }
    }

    if (!is_fg) {
        printf("[%d] + Running   %s\n", job->job_number, job->cmdline);
        fflush(stdout);
        return;
    }

    printf("%s\n", job->cmdline);
    fflush(stdout);

    jobs_give_terminal(job->pgid); 

    if (has_timeout) {
        alarm_fired = 0;
        alarm(timeout_secs); 
    }

    int status;
    pid_t w;
    for (;;) {
        w = waitpid(job->pgid, &status, WUNTRACED); 
        if (w < 0 && errno == EINTR) {
            if (has_timeout && alarm_fired) {
                break;
            }
            continue; 
        }
        break;
    }

    if (has_timeout) {
        alarm(0); 
    }

    if (has_timeout && alarm_fired) {
        alarm_fired = 0;
        kill(-job->pgid, SIGTERM);
        printf("resume: job timed out\n");
        fflush(stdout);
        job->in_use = 0;
        for (int p = 0; p < job->num_procs; p++) {
            job->procs[p].in_use = 0;
        }
        jobs_reclaim_terminal();
        return;
    }

    if (w > 0 && WIFSTOPPED(status)) {
        jobs_foreground_stopped(job->pgid); 
        return;
    }

    for (int p = 0; p < job->num_procs; p++) {
        if (job->procs[p].pid == job->pgid) {
            job->procs[p].in_use = 0;
        }
    }
    update_job_liveness(job);
    jobs_reclaim_terminal(); 
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
        size_t need = len + piece_len + 2; 
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