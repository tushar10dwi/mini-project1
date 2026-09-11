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

static int run_segment(const token_t *seg, int background)
{
    if (seg == NULL) {
        return 0;
    }

    if (seg->type == TOK_WORD &&
        (strcmp(seg->value, "hop") == 0 ||
         strcmp(seg->value, "reveal") == 0 ||
         strcmp(seg->value, "peek") == 0 ||
         strcmp(seg->value, "locate") == 0 ||
         strcmp(seg->value, "activities") == 0 ||
         strcmp(seg->value, "resume") == 0)) {

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
    int prev_was_eof = 0; 

    prompt_init();
    hop_init();
    jobs_init();

    for (;;) {
        jobs_flush_pending();
        prompt_print();

        if (!read_input(line, sizeof(line))) {
            if (jobs_take_sigint()) {
                clearerr(stdin);
                putchar('\n');
                continue;
            }

            if (jobs_take_sigtstp()) {
                clearerr(stdin);
                putchar('\n');
                continue;
            }

            if (jobs_have_stopped() && !prev_was_eof) {
                printf("cshell: there are stopped jobs\n");
                prev_was_eof = 1;
                clearerr(stdin);
                continue;
            }
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
            sequence_execute(tokens, run_segment);

            token_list_free(&tokens);
        }
    }

    jobs_hangup_all();

    hop_save();
    return 0;
}