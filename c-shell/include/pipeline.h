#ifndef PIPELINE_H
#define PIPELINE_H

#include "token.h"

/* Parses tokens separated by TOK_PIPE and executes them as a pipeline */
int pipeline_execute(const token_t *tokens);

#endif