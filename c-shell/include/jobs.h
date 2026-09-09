#ifndef JOBS_H
#define JOBS_H

#include "token.h"
#include <sys/types.h>

/* Installs the SIGCHLD handler. Call this once, near the start of main(),
 * before any background job can possibly be launched. */
void jobs_init(void);

/*
 * Registers pid as a new background job: assigns it the next
 * session-wide job number (monotonically increasing, never reused --
 * requirement D2.4), stores cmdline for later use in its completion
 * message, and immediately prints "[job_number] pid\n" (requirement
 * D2.3).
 *
 * cmdline is copied internally; the caller keeps ownership of its own
 * copy and may (should) free it right after calling this.
 */
void jobs_add(pid_t pid, const char *cmdline);

/*
 * Marks whether a foreground command is currently running (1) or not
 * (0). While active, background-completion messages are queued
 * instead of printed immediately, so they never interleave with a
 * foreground command's own output (requirement D2.11). Call
 * jobs_flush_pending() once the foreground command finishes to print
 * anything that queued up while it ran.
 */
void jobs_set_foreground(int active);

/*
 * Prints and clears any background-completion messages that queued up
 * while a foreground command was running. Call this right before
 * showing the next prompt.
 */
void jobs_flush_pending(void);

/*
 * Reconstructs the human-readable text of a token segment (TOK_WORD
 * values, and '|', '<', '>', '>>' for the corresponding tokens, joined
 * by single spaces) for use in a background-completion message, e.g.
 * "sleep 5" or "sort file.txt | uniq -c". Returns a malloc'd string
 * the caller must free. Returns NULL only on allocation failure.
 */
char *jobs_stringify_tokens(const token_t *tokens);

#endif /* JOBS_H */