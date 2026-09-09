#include "sequence.h"

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
    int stop = 0; /* set once a foreground segment fails */

    token_t *seg_start = (token_t *)tokens;
    token_t *prev = NULL;
    token_t *t = (token_t *)tokens;

    while (t != NULL) {
        if (t->type == TOK_SEMI || t->type == TOK_AMP) {
            int background = (t->type == TOK_AMP);
            token_t *after = t->next;

            if (prev != NULL) {
                if (num_cuts < MAX_SEGMENTS) {
                    cuts[num_cuts].node = prev;
                    cuts[num_cuts].saved_next = prev->next;
                    num_cuts++;
                }
                prev->next = NULL;
                last_status = run_segment(seg_start, background);
            } else {
                /* empty segment before this separator (leading/consecutive) */
                last_status = run_segment(NULL, background);
            }

            /* Only a failed *foreground* segment stops the sequence -- a
             * background launch is always treated as "succeeded" here,
             * since whatever it's running hasn't had a chance to fail
             * yet (that's reported later, asynchronously, via jobs.c). */
            if (!background && last_status != 0) {
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

    /* run whatever follows the last separator (or the whole list, if
     * none found) as a foreground segment -- but only if nothing
     * earlier in the sequence has already failed */
    if (!stop) {
        last_status = run_segment(seg_start, 0);
    }

    /* undo every temporary cut so the caller's token list is intact again */
    for (int i = num_cuts - 1; i >= 0; i--) {
        cuts[i].node->next = cuts[i].saved_next;
    }

    return last_status;
}