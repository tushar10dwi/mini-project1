#include "parser.h"

#include <stddef.h>

/* Each function takes a pointer to "the rest of the token list" -- a
 * NULL pointer means "no tokens remain" -- exactly mirroring the
 * right-linear grammar's non-terminals, one function per
 * non-terminal, one branch per production. */

static int parse_arg(const token_t *t);
static int parse_cmd(const token_t *t);
static int parse_tgt(const token_t *t);
static int parse_bg(const token_t *t);

/* LINE -> e | WORD ARG */
int parser_validate(const token_t *tokens)
{
    if (tokens == NULL) {
        return 1;
    }
    if (tokens->type == TOK_WORD) {
        return parse_arg(tokens->next);
    }
    return 0;
}

/* ARG -> e
 *      | WORD ARG
 *      | OP_LT TGT | OP_GT TGT | OP_GTGT TGT
 *      | OP_PIPE CMD | OP_SEMI CMD
 *      | OP_AMP BG
 */
static int parse_arg(const token_t *t)
{
    if (t == NULL) {
        return 1;
    }
    switch (t->type) {
        case TOK_WORD: return parse_arg(t->next);
        case TOK_LT:   return parse_tgt(t->next);
        case TOK_GT:   return parse_tgt(t->next);
        case TOK_GTGT: return parse_tgt(t->next);
        case TOK_PIPE: return parse_cmd(t->next);
        case TOK_SEMI: return parse_cmd(t->next);
        case TOK_AMP:  return parse_bg(t->next);
        default:       return 0;
    }
}

/* CMD -> WORD ARG (no e production: a command can't be empty) */
static int parse_cmd(const token_t *t)
{
    if (t == NULL || t->type != TOK_WORD) {
        return 0;
    }
    return parse_arg(t->next);
}

/* TGT -> WORD ARG (no e production: a redirect target can't be empty) */
static int parse_tgt(const token_t *t)
{
    if (t == NULL || t->type != TOK_WORD) {
        return 0;
    }
    return parse_arg(t->next);
}

/* BG -> e | WORD ARG (trailing '&' may end the line, or start another command) */
static int parse_bg(const token_t *t)
{
    if (t == NULL) {
        return 1;
    }
    if (t->type != TOK_WORD) {
        return 0;
    }
    return parse_arg(t->next);
}