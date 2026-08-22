#include "input.h"
#include "prompt.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[INPUT_MAX_LEN + 1];

    prompt_init();

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
         * Turning the token list into commands to execute is added
         * in a later part; for now just discard it. */
        token_list_free(&tokens);
    }

    return 0;
}