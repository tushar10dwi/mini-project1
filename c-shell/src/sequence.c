#include "sequence.h"
#include "prompt.h"
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

/* NOTE: assumes token.h defines a TOK_SEMI token type for ';', matching
 * the existing TOK_PIPE / TOK_LT / TOK_GT / TOK_GTGT naming. If your
 * token.h uses a different name (e.g. TOK_SEMICOLON), update the two
 * references below accordingly. */

#define MAX_SEGMENTS 64

int sequence_execute(const token_t *tokens, segment_executor_t run_segment)
{
    typedef struct {
        token_t *node;        /* node whose ->next we temporarily cleared */
        token_t *saved_next;  /* its original ->next, to restore later    */
    } cut_t;

    cut_t cuts[MAX_SEGMENTS];
    int num_cuts = 0;
    int last_status = 0;
    int stop = 0; /* set once a segment fails; no further segments run */

    token_t *seg_start = (token_t *)tokens;
    token_t *prev = NULL;
    token_t *t = (token_t *)tokens;

    while (t != NULL) {
        if (t->type == TOK_SEMI) {
            token_t *after = t->next;

            if (prev != NULL) {
                if (num_cuts < MAX_SEGMENTS) {
                    cuts[num_cuts].node = prev;
                    cuts[num_cuts].saved_next = prev->next;
                    num_cuts++;
                }
                prev->next = NULL;
                last_status = run_segment(seg_start);
            } else {
                /* empty segment before this ';' (leading/consecutive ';') */
                last_status = run_segment(NULL);
            }

            if (last_status != 0) {
                /* a command failed to execute: stop the sequence here and
                 * do not run any of the remaining ';'-separated commands */
                stop = 1;
                break;
            }

            seg_start = after;
            prev = NULL;
            t = after;
            continue;
        }

        prev = t;
        t = t->next;
    }

    /* run whatever follows the last ';' (or the whole list, if none found) --
     * but only if nothing earlier in the sequence has already failed */
    if (!stop) {
        last_status = run_segment(seg_start);
    }

    /* undo every temporary cut so the caller's token list is intact again */
    for (int i = num_cuts - 1; i >= 0; i--) {
        cuts[i].node->next = cuts[i].saved_next;
    }

    return last_status;
}