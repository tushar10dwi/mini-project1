#ifndef SPY_H
#define SPY_H

/* Part F1: `spy [pid]` -- lists open files of a process (cwd, exe,
 * memory-mapped files, numeric fds) by reading /proc/<pid>. With no
 * pid, reports on the running shell itself. */
void spy_execute(int argc, char **argv);

#endif