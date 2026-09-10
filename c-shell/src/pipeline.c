#define _POSIX_C_SOURCE 200809L

#include "pipeline.h"
#include "exec.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>

#define MAX_CMDS 64
#define MAX_ARGS 64

int pipeline_execute(const token_t *tokens, int background)
{
    char *cmd_argv[MAX_CMDS][MAX_ARGS];
    int cmd_argc[MAX_CMDS] = {0};
    int num_cmds = 0;

    // 1. Separate tokens into individual command arrays by TOK_PIPE
    const token_t *t = tokens;
    while (t != NULL && num_cmds < MAX_CMDS) {
        if (t->type == TOK_PIPE) { // Ensure TOK_PIPE is defined in token.h
            cmd_argv[num_cmds][cmd_argc[num_cmds]] = NULL;
            num_cmds++;
            t = t->next;
            continue;
        }

        if (cmd_argc[num_cmds] < MAX_ARGS - 1) {
            if (t->type == TOK_WORD) {
                cmd_argv[num_cmds][cmd_argc[num_cmds]++] = t->value;
            } else if (t->type == TOK_LT) {
                cmd_argv[num_cmds][cmd_argc[num_cmds]++] = "<";
            } else if (t->type == TOK_GT) {
                cmd_argv[num_cmds][cmd_argc[num_cmds]++] = ">";
            } else if (t->type == TOK_GTGT) {
                cmd_argv[num_cmds][cmd_argc[num_cmds]++] = ">>";
            }
        }
        t = t->next;
    }
    cmd_argv[num_cmds][cmd_argc[num_cmds]] = NULL;
    num_cmds++;

    // Capture the pipeline's text now (before forking) -- needed for
    // a background job's eventual completion message, and equally for
    // a foreground job's "[N] + Stopped <cmdline>" message if it gets
    // Ctrl-Z'd (E2 req 5).
    char *cmdline = jobs_stringify_tokens(tokens);

    int pipes[MAX_CMDS][2];
    pid_t pids[MAX_CMDS];

    // 2. Create pipes for the pipeline
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("cshell: pipe");
            free(cmdline);
            return -1;
        }
    }

    // 3. Fork a child process for each command
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("cshell: fork");
            free(cmdline);
            return -1;
        }

        if (pids[i] == 0) { // Child Process
            // Join this pipeline's own process group (requirement E1.1:
            // "using setpgid() ... also in every child before execve").
            // The first command becomes its own group leader (pgid ==
            // its own pid); every later stage joins that same group.
            // Every non-pipe command counts as a group of one (E1.2).
            if (i == 0) {
                setpgid(0, 0);
            } else {
                setpgid(0, pids[0]);
            }

            // SIGTTIN/SIGTTOU are delivered to the whole process group,
            // not just the one process that touched the terminal -- so
            // without this split, a later pipeline stage (whose stdin
            // is a pipe, never the terminal) would still be stopped as
            // a bystander whenever an earlier stage's terminal read
            // triggers the group-wide signal. Only the stage actually
            // connected to the terminal on that side should keep the
            // default (stoppable) disposition; every other stage must
            // ignore it, so the same group-wide signal is simply a
            // no-op for them and they keep running/blocking normally.
            // (SIG_IGN survives execve(), so this also had to be reset
            // at all -- otherwise everyone would inherit the shell's
            // own ignore-disposition and never stop, per D2 #12/E1.)
            int stdin_is_terminal  = (i == 0);
            int stdout_is_terminal = (i == num_cmds - 1);
            signal(SIGTTIN, stdin_is_terminal  ? SIG_DFL : SIG_IGN);
            signal(SIGTTOU, stdout_is_terminal ? SIG_DFL : SIG_IGN);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            // Redirect Stdin from previous pipe (if not first command)
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            // Redirect Stdout to current pipe (if not last command)
            if (i < num_cmds - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Child must close all unused pipe fds before execution
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            if (cmd_argc[i] == 0) exit(0);

            // Execute built-ins or standard commands inside the pipeline segment
            if (strcmp(cmd_argv[i][0], "hop") == 0) {
                hop_execute(cmd_argc[i], cmd_argv[i]);
                exit(0);
            } else if (strcmp(cmd_argv[i][0], "reveal") == 0) {
                reveal_execute(cmd_argc[i], cmd_argv[i]);
                exit(0);
            } else if (strcmp(cmd_argv[i][0], "peek") == 0) {
                peek_execute(cmd_argc[i], cmd_argv[i]);
                exit(0);
            } else if (strcmp(cmd_argv[i][0], "locate") == 0) {
                locate_execute(cmd_argc[i], cmd_argv[i]);
                exit(0);
            } else {
                // If it fails, exec_command outputs "cshell: command not found..." 
                int ret = exec_command(cmd_argc[i], cmd_argv[i]);
                exit(ret == 0 ? 0 : 127);
            }
        }

        // Parent: join the child to the same group from this side too
        // (the standard belt-and-suspenders pattern -- whichever of
        // parent/child runs first "wins", closing the race where the
        // child might exec, or the parent might tcsetpgrp/signal the
        // group, before the other side has set it).
        setpgid(pids[i], pids[0]);

        if (i == 0 && !background) {
            // Foreground: hand the terminal to this new group so it
            // can read from stdin normally and receive ^C/^Z directly.
            jobs_give_terminal(pids[0]);
        }
    }

    // 4. Parent shell must close every pipe file descriptor
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Names captured up front -- needed both for background tracking
    // (below) and for registering the job if it gets Ctrl-Z'd while
    // still in the foreground (E2 req 5).
    char *names[MAX_CMDS];
    for (int i = 0; i < num_cmds; i++) {
        names[i] = cmd_argv[i][0] != NULL ? cmd_argv[i][0] : "";
    }

    if (background) {
        jobs_add(pids, names, num_cmds, cmdline); // reports pids[0], requirement D2.13/E1.8
        free(cmdline);
        return 0;
    }

    int last_status = 0;
    int stopped = 0;
    for (int i = 0; i < num_cmds; i++) {
        int status;
        // WUNTRACED: notice Ctrl-Z stopping this stage, not just exit.
        waitpid(pids[i], &status, WUNTRACED);
        if (WIFSTOPPED(status)) {
            stopped = 1;
            continue;
        }
        if (i == num_cmds - 1 && !stopped) {
            // A pipeline's overall exit status follows its last stage,
            // matching ordinary shell convention -- this is what lets
            // sequence_execute() correctly detect a failed command and
            // stop the rest of a ';'-sequence.
            last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }

    if (stopped) {
        // Wasn't tracked as a job until now (only background jobs
        // were) -- register it so `activities`, Ctrl-D's stopped-job
        // check, and shell-exit SIGHUP can all see it.
        jobs_add_stopped(pids, names, num_cmds, cmdline);
        jobs_foreground_stopped(pids[0]); // marks Stopped, prints "[N] + Stopped ...", reclaims terminal
    } else {
        // Reclaim the terminal for the shell now that the foreground
        // job is done (jobs_init() already set SIGTTOU to be ignored
        // here, so this call can't stop the shell itself).
        jobs_reclaim_terminal();
    }
    free(cmdline);

    return last_status;
}