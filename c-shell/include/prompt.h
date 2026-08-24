#ifndef PROMPT_H
#define PROMPT_H

void prompt_init(void);
void prompt_print(void);

/*
 * prompt_home_dir
 * ---------------
 * Returns the shell's home directory: the CWD the shell was started
 * in, as captured by prompt_init(). The returned pointer is valid for
 * the lifetime of the program; callers must not modify or free it.
 */
const char *prompt_home_dir(void);

#endif