#include "input.h"
#include "prompt.h"

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
    }

    return 0;
}