#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef enum {
    LEX_OK,
    LEX_ERROR,
} lex_status_t;

/*
 * lex_line
 * --------
 * Tokenizes a single input line (already newline-stripped; there is
 * no line continuation or multi-line input) into a linked list of
 * tokens per the shell's lexical grammar (character classes, token
 * classes, maximal munch, and the quoting rules).
 *
 * On success sets *out to the head of the token list (NULL for an
 * empty or whitespace-only line) and returns LEX_OK.
 *
 * On a lexical error -- a quote opened and never closed before end
 * of line, or a '\' as the final character of the line -- returns
 * LEX_ERROR and leaves *out as an empty list. lex_line does not
 * print anything; the caller reports "cshell: invalid syntax".
 */
lex_status_t lex_line(const char *line, token_t **out);

#endif /* LEXER_H */