#ifndef SNOOP_H
#define SNOOP_H

/* Part F2: `snoop command [args...]` or `snoop -p pid` -- ptrace-based
 * syscall tracer. Runs the target to completion (or until the
 * attached process exits) and prints a summary table of syscalls by
 * call count. */
void snoop_execute(int argc, char **argv);

#endif