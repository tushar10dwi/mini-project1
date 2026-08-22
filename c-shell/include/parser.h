#ifndef PARSER_H
#define PARSER_H

#include "token.h"

/*
 * parser_validate
 * ---------------
 * Validates a token list against the shell's grammar:
 *
 *   LINE -> e | WORD ARG
 *   ARG  -> e | WORD ARG | OP_LT TGT | OP_GT TGT | OP_GTGT TGT
 *             | OP_PIPE CMD | OP_SEMI CMD | OP_AMP BG
 *   CMD  -> WORD ARG
 *   TGT  -> WORD ARG
 *   BG   -> e | WORD ARG
 *
 * Returns 1 if tokens forms a valid LINE, 0 otherwise. Does not
 * print anything; the caller decides how to report an invalid line.
 */
int parser_validate(const token_t *tokens);

#endif /* PARSER_H */