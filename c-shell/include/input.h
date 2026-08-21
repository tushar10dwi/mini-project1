#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

/* Per spec: user input can be assumed to be at most this many chars
 * (not counting the terminating newline/null). */
#define INPUT_MAX_LEN 1024

/*
 * read_input
 * ----------
 * Reads a single line of input from stdin into buf (a buffer of at
 * least bufsize bytes), consuming it once the user presses
 * enter/return. Strips the trailing newline, if any.
 *
 * Returns 1 on success, or 0 on EOF / read error (in which case buf
 * is left unspecified and the caller should stop reading).
 */
int read_input(char *buf, size_t bufsize);

#endif /* INPUT_H */