#ifndef JOBS_H
#define JOBS_H

#include "token.h"
#include <sys/types.h>

void jobs_init(void);

pid_t jobs_get_shell_pgid(void);
void jobs_add(const pid_t *pids, char *const *names, int count, const char *cmdline);

void jobs_set_foreground(int active);

void jobs_flush_pending(void);

void jobs_print_activities(void);
char *jobs_stringify_tokens(const token_t *tokens);

void jobs_give_terminal(pid_t pgid);
void jobs_reclaim_terminal(void);
void jobs_foreground_stopped(pid_t pgid);
int  jobs_have_stopped(void);
void jobs_hangup_all(void);
void jobs_add_stopped(const pid_t *pids, char *const *names, int count, const char *cmdline);
int  jobs_take_sigint(void);
int  jobs_take_sigtstp(void);
void jobs_resume_execute(int argc, char **argv);


#endif 