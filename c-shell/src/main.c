#include "input.h"
#include "prompt.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"
#include "sequence.h"
#include "jobs.h"
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

/* Decides how to run one segment. Builtins (hop/reveal/peek/locate/
 * activities) always run inline, synchronously, in the shell's own
 * process -- backgrounding a builtin isn't meaningful, so '&' is
 * simply ignored for them.
 *
 * Everything else -- a single external command or a multi-stage
 * pipeline -- is handed to pipeline_execute(), which treats a lone
 * command as a one-stage pipeline. Routing both cases through the
 * same function gives every external command its own process group
 * (E1 #1/#2) and, when backgrounded, registers it uniformly for
 * `activities` and D2 completion reporting.
 *
 * background is 1 for a segment terminated by '&' (must be launched
 * without waiting), 0 for a plain foreground segment. Passed to
 * sequence_execute() as the callback it uses per segment.
 */
static int run_segment(const token_t *seg, int background)
{
    if (seg == NULL) {
        /* empty segment, e.g. from "cmd1;;cmd2" or a stray leading separator */
        return 0;
    }

    if (seg->type == TOK_WORD &&
        (strcmp(seg->value, "hop") == 0 ||
         strcmp(seg->value, "reveal") == 0 ||
         strcmp(seg->value, "peek") == 0 ||
         strcmp(seg->value, "locate") == 0 ||
         strcmp(seg->value, "activities") == 0)) {

        char *argv[MAX_ARGS];
        int argc = build_argv(seg, argv, MAX_ARGS);
        if (argc == 0) {
            return 0;
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
            jobs_print_activities();
        }

        return 0;
    }

    if (background) {
        return pipeline_execute(seg, 1);
    }

    jobs_set_foreground(1);
    int status = pipeline_execute(seg, 0);
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
        /* print any background-completion messages that queued up
         * while the last foreground command was running, then show
         * the next prompt (requirement D2.5 / D2.11) */
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
            /* sequence_execute() splits on ';' and '&' and calls
             * run_segment() for each piece, in order -- run_segment()
             * then decides whether that piece is a builtin, a single
             * command, or a pipeline, and whether it must be waited
             * for (foreground) or just launched (background). */
            sequence_execute(tokens, run_segment);

            token_list_free(&tokens);
        }
    }

    hop_save();
    return 0;
}