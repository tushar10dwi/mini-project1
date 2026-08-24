#ifndef HOP_H
#define HOP_H

/*
 * hop_init
 * --------
 * Loads the persistent frecency store from disk (~/.cshell_hop_frecency,
 * falling back to the shell's home_dir if $HOME is unset). Call once at
 * shell startup, after prompt_init().
 */
void hop_init(void);

/*
 * hop_save
 * --------
 * Flushes the frecency store back to disk if it changed during this
 * session. Call once at shell shutdown.
 */
void hop_save(void);

/*
 * hop_execute
 * -----------
 * Runs the hop builtin. argv[0] is "hop"; argv[1..argc-1] are the
 * arguments, processed sequentially per the grammar
 *   hop ((~ | . | .. | - | name)*)?
 * Returns 0 if every argument resolved successfully, -1 if any
 * argument failed to resolve (an error has already been printed to
 * stdout, and remaining arguments are not processed).
 */
int hop_execute(int argc, char **argv);

#endif /* HOP_H */