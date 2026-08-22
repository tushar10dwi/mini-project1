#include "lexer.h"
#include "input.h" /* INPUT_MAX_LEN: an upper bound on any single word */

#include <stdlib.h>
#include <string.h>

/* space -> ' ' | \t | \n | \r */
static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* special -> | & > < ; */
static int is_special_char(char c)
{
    return c == '|' || c == '&' || c == '>' || c == '<' || c == ';';
}

/*
 * Reads one WORD (fragment+) starting at line[*pos] and advances
 * *pos past it. A word is a maximal run of concatenated fragments:
 * it keeps growing across ordinary characters, escapes, and quoted
 * sections with no separating whitespace, and only ends at
 * whitespace, an unquoted special character, or end of line.
 *
 * Returns a newly malloc'd, NUL-terminated string on success. On a
 * lexical error (unterminated quote, or a trailing backslash at end
 * of line) returns NULL and leaves *pos unspecified.
 */
static char *lex_word(const char *line, size_t *pos)
{
    char buf[INPUT_MAX_LEN + 1];
    size_t len = 0;
    size_t i = *pos;

    while (line[i] != '\0' && !is_space_char(line[i]) && !is_special_char(line[i])) {
        char c = line[i];

        if (c == '\\') {
            /* Unquoted escape: \c contributes c literally, backslash
             * removed. A trailing '\' with nothing after it is a
             * lexical error. */
            if (line[i + 1] == '\0') {
                return NULL;
            }
            if (len < INPUT_MAX_LEN) {
                buf[len++] = line[i + 1];
            }
            i += 2;
        } else if (c == '"') {
            /* "..." : \" -> ", \\ -> \, any other \c stays as both
             * characters, everything else verbatim. */
            i++;
            for (;;) {
                char q = line[i];
                if (q == '\0') {
                    return NULL; /* unterminated quote */
                }
                if (q == '"') {
                    i++;
                    break;
                }
                if (q == '\\') {
                    char n = line[i + 1];
                    if (n == '\0') {
                        return NULL; /* unterminated quote */
                    }
                    if (n == '"') {
                        if (len < INPUT_MAX_LEN) buf[len++] = '"';
                    } else if (n == '\\') {
                        if (len < INPUT_MAX_LEN) buf[len++] = '\\';
                    } else {
                        if (len < INPUT_MAX_LEN) buf[len++] = '\\';
                        if (len < INPUT_MAX_LEN) buf[len++] = n;
                    }
                    i += 2;
                } else {
                    if (len < INPUT_MAX_LEN) buf[len++] = q;
                    i++;
                }
            }
        } else if (c == '\'') {
            /* '...' : contributed verbatim, no escape processing. */
            i++;
            for (;;) {
                char q = line[i];
                if (q == '\0') {
                    return NULL; /* unterminated quote */
                }
                if (q == '\'') {
                    i++;
                    break;
                }
                if (len < INPUT_MAX_LEN) buf[len++] = q;
                i++;
            }
        } else {
            /* ordinary character */
            if (len < INPUT_MAX_LEN) buf[len++] = c;
            i++;
        }
    }

    buf[len] = '\0';
    *pos = i;

    char *word = malloc(len + 1);
    if (word != NULL) {
        memcpy(word, buf, len + 1);
    }
    return word;
}

lex_status_t lex_line(const char *line, token_t **out)
{
    *out = NULL;
    size_t i = 0;

    while (line[i] != '\0') {
        char c = line[i];

        if (is_space_char(c)) {
            i++;
            continue;
        }

        /* Operators, with maximal munch for >> vs >. */
        if (c == '>') {
            if (line[i + 1] == '>') {
                token_list_append(out, TOK_GTGT, NULL);
                i += 2;
            } else {
                token_list_append(out, TOK_GT, NULL);
                i += 1;
            }
            continue;
        }
        if (c == '<') { token_list_append(out, TOK_LT, NULL); i++; continue; }
        if (c == '|') { token_list_append(out, TOK_PIPE, NULL); i++; continue; }
        if (c == '&') { token_list_append(out, TOK_AMP, NULL); i++; continue; }
        if (c == ';') { token_list_append(out, TOK_SEMI, NULL); i++; continue; }

        /* Otherwise this begins a WORD (ordinary char, escape, or quote). */
        char *word = lex_word(line, &i);
        if (word == NULL) {
            token_list_free(out);
            return LEX_ERROR;
        }
        token_list_append(out, TOK_WORD, word);
        free(word);
    }

    return LEX_OK;
}