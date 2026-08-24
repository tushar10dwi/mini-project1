#include "input.h"
#include "prompt.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include <stdio.h>
#include <string.h>

#define MAX_ARGS 64

/*
 * Builds argv from the leading run of TOK_WORD tokens (stopping at the
 * first operator or end of list). This is a stand-in for the full
 * command dispatcher (pipes, redirection, backgrounding, external
 * commands) that later parts will add; for now it's just enough to
 * route a single simple command like "hop a b" to its builtin.
 *
 * Returns argc. argv[i] points into the tokens' owned strings, so it
 * is only valid until the token list is freed.
 */
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

int main(void)
{
    char line[INPUT_MAX_LEN + 1];

    prompt_init();
    hop_init();

    for (;;) {
        prompt_print();

        if (!read_input(line, sizeof(line))) {
            /* EOF (e.g. Ctrl-D) or a read error: exit the shell. */
            putchar('\n');
            break;
        }

        /* Part A only handles reading input and re-prompting.
         * Parsing and execution are added in later parts. */
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
 
        /* Line is syntactically valid (or empty/whitespace-only).
         * Full command execution (pipes, redirection, external
         * commands) is added in a later part; for now, route simple
         * builtins like hop directly. */
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
        }

        token_list_free(&tokens);
    }

    hop_save();
    return 0;
}