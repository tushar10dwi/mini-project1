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
#include <stdio.h>
#include <string.h>

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

int main(void)
{
    char line[INPUT_MAX_LEN + 1];

    prompt_init();
    hop_init();

    for (;;) {
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
 
        if (tokens != NULL && tokens->type == TOK_WORD) {
            char *argv[MAX_ARGS];
            int argc = build_argv(tokens, argv, MAX_ARGS);
            
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
                int exec_argc = build_exec_argv(tokens, exec_argv, MAX_ARGS);
                exec_command(exec_argc, exec_argv);
            }
        }

        token_list_free(&tokens);
    }

    hop_save();
    return 0;
}