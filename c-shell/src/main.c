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
#include "spy.h"
#include "snoop.h"
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
 * activities/resume/ping/spy/snoop) always run inline, synchronously, in the shell's own
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
         strcmp(seg->value, "activities") == 0 ||
         strcmp(seg->value, "resume") == 0 ||
         strcmp(seg->value, "ping") == 0 ||
         strcmp(seg->value, "spy") == 0 ||
         strcmp(seg->value, "snoop") == 0)) {

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
        else if (strcmp(argv[0], "resume") == 0) {
            jobs_resume_execute(argc, argv);
        }
        else if (strcmp(argv[0], "ping") == 0) {
            jobs_ping_execute(argc, argv);
        }
        else if (strcmp(argv[0], "spy") == 0) {
            spy_execute(argc, argv);
        }
        else if (strcmp(argv[0], "snoop") == 0) {
            snoop_execute(argc, argv);
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
    int prev_was_eof = 0; /* E2 req 8: track a bare Ctrl-D immediately before this one */

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
            /* E2 req 1: Ctrl-C at the prompt must not exit or touch
             * anything -- just redraw the prompt. Checked first and
             * separately from real EOF below, since read_input()
             * failing doesn't by itself tell us which one happened. */
            if (jobs_take_sigint()) {
                clearerr(stdin);
                putchar('\n');
                continue;
            }

            /* E2 req 1: same idea for Ctrl-Z hitting the shell itself
             * (idle prompt, no foreground job to stop instead) --
             * must not be mistaken for Ctrl-D and exit. */
            if (jobs_take_sigtstp()) {
                clearerr(stdin);
                putchar('\n');
                continue;
            }

            /* E2 req 9: read_input() only reports this as EOF when the
             * line was empty. */
            if (jobs_have_stopped() && !prev_was_eof) {
                /* E2 req 7 */
                printf("cshell: there are stopped jobs\n");
                prev_was_eof = 1;
                /* fgets() (inside read_input()) latches stdin's EOF
                 * flag once it hits real EOF -- every later read on
                 * the stream short-circuits to EOF immediately without
                 * this, which is what made it look like exit happened
                 * after just one Ctrl-D. Clearing it makes the next
                 * read_input() actually block for a genuine second
                 * keypress. */
                clearerr(stdin);
                continue;
            }
            /* E2 req 6 / req 8: no stopped jobs, or a second Ctrl-D in a row */
            putchar('\n');
            break;
        }
        prev_was_eof = 0;

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

    /* E2 req 10 */
    jobs_hangup_all();

    hop_save();
    return 0;
}