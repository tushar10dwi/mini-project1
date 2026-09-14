// schedulertest.c -- generic scheduler exerciser used across RR,
// FIFO, and MLFQ builds (2.2, 2.3.2, 2.3.3). A mix of CPU-bound and
// I/O-bound children, so the resulting log shows queue movement
// under MLFQ and gives every scheduler a comparable workload.
//
// Not part of default xv6 -- add "$U/_schedulertest\" to UPROGS in
// the Makefile to build it.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Pure CPU burn -- no syscalls, so this genuinely consumes the
// process's time slice instead of returning to the kernel early.
// `iters` is unitless; tune BURN_UNIT below against your own QEMU
// speed so bursts actually span several ticks.
static void
burn(long iters)
{
  volatile long x = 0;
  for (long i = 0; i < iters; i++)
    x += i;
}

// Roughly how many burn() iterations correspond to ~1 timer tick on
// your machine. Timer ticks fire about every tenth of a second (see
// clockintr() in kernel/trap.c); if your MLFQLOG output shows a
// child finishing its whole burst inside a single tick, raise this.
// If bursts are dragging out far longer than expected, lower it.
#define BURN_UNIT 150000000

struct child_spec {
  const char *role;
  int bursts;       // number of compute/yield rounds
  long burn_iters;  // CPU work per round, in burn() iterations
  int sleep_ticks;  // 0 = never voluntarily yields (pure CPU-bound)
};

// Five processes, deliberately spanning the range from "pure
// CPU-bound, never yields" to "mostly I/O, yields constantly" --
// enough spread that the plot should show clearly different queue
// trajectories.
static struct child_spec specs[] = {
    {"cpu-long", 60, BURN_UNIT, 0},
    {"cpu-medium", 35, BURN_UNIT, 0},
    {"cpu-short", 15, BURN_UNIT, 0},
    {"io-bound-fast", 50, BURN_UNIT / 8, 2},
    {"io-bound-slow", 25, BURN_UNIT / 8, 6},
};
#define NCHILDREN (int)(sizeof(specs) / sizeof(specs[0]))

// xv6's user-space printf() issues one write() syscall per
// character. With several children genuinely running in parallel
// (this Makefile defaults to CPUS=3), that lets two children's
// announcements interleave on the console byte-by-byte. Build the
// whole line ourselves and hand it to write() in one syscall instead
// -- the console holds its lock for the full duration of one write()
// call, so this line can't get spliced with another process's.
static void
announce_child(int pid, const char *role)
{
  char buf[64];
  int i = 0;
  const char *prefix = "SCHEDTEST_CHILD pid=";
  const char *mid = " role=";
  const char *p;

  for (p = prefix; *p; p++)
    buf[i++] = *p;

  {
    char tmp[12];
    int t = 0, v = pid;
    if (v == 0) {
      tmp[t++] = '0';
    } else {
      while (v > 0) {
        tmp[t++] = '0' + (v % 10);
        v /= 10;
      }
    }
    while (t > 0)
      buf[i++] = tmp[--t];
  }

  for (p = mid; *p; p++)
    buf[i++] = *p;
  for (p = role; *p; p++)
    buf[i++] = *p;
  buf[i++] = '\n';

  write(1, buf, i);
}

int
main(int argc, char *argv[])
{
  int i;

  printf("SCHEDTEST_START\n");

  for (i = 0; i < NCHILDREN; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("schedulertest: fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      struct child_spec *s = &specs[i];
      // Print this once, right away, so the plotting script can map
      // this pid to a role before any MLFQLOG lines for it appear.
      announce_child(getpid(), s->role);
      for (int b = 0; b < s->bursts; b++) {
        burn(s->burn_iters);
        if (s->sleep_ticks > 0)
          pause(s->sleep_ticks);
      }
      exit(0);
    }
  }

  for (i = 0; i < NCHILDREN; i++)
    wait(0);

  printf("SCHEDTEST_END\n");
  exit(0);
}