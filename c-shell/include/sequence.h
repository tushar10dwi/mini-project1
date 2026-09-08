#ifndef SEQUENCE_H
#define SEQUENCE_H

#include "token.h"

/*
 * Runs one ';'-delimited segment (a sub-list of tokens containing no
 * TOK_SEMI) to completion and returns its exit status. Supplied by the
 * caller of sequence_execute() so that sequence.c stays agnostic to what
 * a segment actually is (a single command, a pipeline, etc).
 */
typedef int (*segment_executor_t)(const token_t *segment);

/*
 * Splits a token list on ';' (TOK_SEMI) into segments and runs each one,
 * strictly left to right, via the supplied executor callback -- waiting
 * for each segment to finish before starting the next.
 *
 * If a segment fails (run_segment returns non-zero, e.g. because
 * exec_command couldn't find the command), the sequence stops immediately
 * and none of the remaining ';'-separated segments are run.
 *
 * The input token list is left logically unchanged when this function
 * returns (any internal splitting is undone), so the caller's normal
 * token_list_free() continues to work as-is.
 *
 * Returns the exit status of the last segment that was executed.
 */
int sequence_execute(const token_t *tokens, segment_executor_t run_segment);

#endif /* SEQUENCE_H */