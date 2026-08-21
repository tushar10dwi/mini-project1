#include "input.h"

#include <stdio.h>
#include <string.h>

int read_input(char *buf, size_t bufsize)
{
    if (fgets(buf, (int)bufsize, stdin) == NULL) {
        /* EOF (Ctrl-D) or a read error. */
        return 0;
    }

    /* The input is consumed as soon as enter/return is pressed;
     * fgets() already blocks until that happens. Strip the newline
     * fgets() leaves in place so callers get a clean line. */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    return 1;
}