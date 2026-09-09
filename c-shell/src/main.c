#include "input.h"
#include "prompt.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"
#include "exec.h"
#include "sequence.h"
#include "jobs.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 64

static int build_argv(const token_t *tokens, char **argv, int max_args)
{
    int argc = 0;
    const token_t *t = tokens;

    while (t != NULL && t->type == TOK_WORD && argc < max_args) {
        argv[argc++] = t->value;
        t = t->next;
    }

    return argc;
}

static int build_exec_argv(const token_t *tokens, char **argv, int max_args)
{
    int argc = 0;

    for (const token_t *t = tokens;
         t != NULL && argc < max_args - 1;
         t = t->next) {

        if (t->type == TOK_WORD) {
            argv[argc++] = t->value;
        }
        else if (t->type == TOK_LT) {
            argv[argc++] = "<";
        }
        else if (t->type == TOK_GT) {
            argv[argc++] = ">";
        }
        else if (t->type == TOK_GTGT) {
            argv[argc++] = ">>";
        }
    }

    argv[argc] = NULL;
    return argc;
}

static int run_single_background(const token_t *seg, char **argv, int argc)
{
    char *cmdline = jobs_stringify_tokens(seg);

    pid_t pid = fork();
    if (pid < 0) {
        perror("cshell: fork");
        free(cmdline);
        return -1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        if (strcmp(argv[0], "hop") == 0) {
            hop_execute(argc, argv);
        }
        else if (strcmp(argv[0], "reveal") == 0) {
            reveal_execute(argc, argv);
        }
        else if (strcmp(argv[0], "peek") == 0) {
            peek_execute(argc, argv);
        }
        else if (strcmp(argv[0], "locate") == 0) {
            locate_execute(argc, argv);
        }
        else {
            char *exec_argv[MAX_ARGS];
            int exec_argc = build_exec_argv(seg, exec_argv, MAX_ARGS);
            int ret = exec_command(exec_argc, exec_argv);
            exit(ret == 0 ? 0 : 127);
        }
        exit(0);
    }

    jobs_add(pid, cmdline);
    free(cmdline);
    return 0;
}

static int run_segment(const token_t *seg, int background)
{
    if (seg == NULL) {
        return 0;
    }

    for (const token_t *t = seg; t != NULL; t = t->next) {
        if (t->type == TOK_PIPE) {
            if (background) {
                return pipeline_execute(seg, 1);
            }
            jobs_set_foreground(1);
            int status = pipeline_execute(seg, 0);
            jobs_set_foreground(0);
            return status;
        }
    }

    if (seg->type != TOK_WORD) {
        return 0;
    }

    char *argv[MAX_ARGS];
    int argc = build_argv(seg, argv, MAX_ARGS);
    if (argc == 0) {
        return 0;
    }

    if (background) {
        return run_single_background(seg, argv, argc);
    }

    jobs_set_foreground(1);
    int status = 0;

    if (strcmp(argv[0], "hop") == 0) {
        hop_execute(argc, argv);
    }
    else if (strcmp(argv[0], "reveal") == 0) {
        reveal_execute(argc, argv);
    }
    else if (strcmp(argv[0], "peek") == 0) {
        peek_execute(argc, argv);
    }
    else if (strcmp(argv[0], "locate") == 0) {
        locate_execute(argc, argv);
    }
    else {
        char *exec_argv[MAX_ARGS];
        int exec_argc = build_exec_argv(seg, exec_argv, MAX_ARGS);
        status = exec_command(exec_argc, exec_argv);
    }

    jobs_set_foreground(0);
    return status;
}

int main(void)
{
    char line[INPUT_MAX_LEN + 1];

    prompt_init();
    hop_init();
    jobs_init();

    for (;;) {
        jobs_flush_pending();
        prompt_print();

        if (!read_input(line, sizeof(line))) {
            putchar('\n');
            break;
        }

        if (strcmp(line, "exit") == 0) {
            break;
        }

        token_t *tokens = NULL;
        if (lex_line(line, &tokens) != LEX_OK) {
            printf("cshell: invalid syntax\n");
            continue;
        }
 
        if (!parser_validate(tokens)) {
            printf("cshell: invalid syntax\n");
            token_list_free(&tokens);
            continue;
        }

        if (tokens != NULL) {

            sequence_execute(tokens, run_segment);

            token_list_free(&tokens);
        }
    }

    hop_save();
    return 0;
}