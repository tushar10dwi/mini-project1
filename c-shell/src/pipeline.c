#include "pipeline.h"
#include "exec.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define MAX_CMDS 64
#define MAX_ARGS 64

int pipeline_execute(const token_t *tokens)
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

    int pipes[MAX_CMDS][2];
    pid_t pids[MAX_CMDS];

    // 2. Create pipes for the pipeline
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("cshell: pipe");
            return -1;
        }
    }

    // 3. Fork a child process for each command
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("cshell: fork");
            return -1;
        }

        if (pids[i] == 0) { // Child Process
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
    }

    // 4. Parent shell must close every pipe file descriptor
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // 5. Parent waits for all commands to complete before continuing
    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}