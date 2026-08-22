#ifndef TOKEN_H
#define TOKEN_H

/* Token classes, per the lexer spec (OP_* operators plus WORD). */
typedef enum {
    TOK_WORD,
    TOK_PIPE,  /* | */
    TOK_AMP,   /* & */
    TOK_SEMI,  /* ; */
    TOK_LT,    /* <  */
    TOK_GT,    /* >  */
    TOK_GTGT,  /* >> */
} token_type_t;

/*
 * A single token, as a node in a singly linked list. `value` holds
 * the decoded text for TOK_WORD tokens (quotes/escapes already
 * resolved) and is NULL for operator tokens.
 *
 * A linked list (rather than a full AST) is enough to validate the
 * grammar now and gives later parts (pipelines, redirections,
 * backgrounding) everything they need to walk the command.
 */
typedef struct token {
    token_type_t type;
    char *value;
    struct token *next;
} token_t;

/*
 * Allocates a new token and appends it to the end of the list rooted
 * at *head. value is copied (pass NULL for operator tokens). Returns
 * the new node, or NULL on allocation failure (in which case *head
 * is left unchanged).
 */
token_t *token_list_append(token_t **head, token_type_t type, const char *value);

/*
 * Frees every node in the list rooted at *head, including each
 * node's owned value string, then sets *head to NULL. Safe to call
 * with *head already NULL.
 */
void token_list_free(token_t **head);

#endif /* TOKEN_H */