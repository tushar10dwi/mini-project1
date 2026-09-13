/* Must come before any system header: without it, strict ISO C builds
 * won't expose ptrace()/execvp()/kill() etc. from their headers. */
#define _POSIX_C_SOURCE 200809L

#include "snoop.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* x86_64 syscall number -> name, straight out of asm/unistd_64.h.
 * Anything not listed here (gaps, or numbers past the end of this
 * table) is unknown to us and printed as "syscall_N" per req 7 --
 * this table doesn't need to be exhaustive for that to be correct. */
static const char *const syscall_names[329] = {
    [0] = "read", [1] = "write", [2] = "open", [3] = "close",
    [4] = "stat", [5] = "fstat", [6] = "lstat", [7] = "poll",
    [8] = "lseek", [9] = "mmap", [10] = "mprotect", [11] = "munmap",
    [12] = "brk", [13] = "rt_sigaction", [14] = "rt_sigprocmask",
    [15] = "rt_sigreturn", [16] = "ioctl", [17] = "pread64",
    [18] = "pwrite64", [19] = "readv", [20] = "writev", [21] = "access",
    [22] = "pipe", [23] = "select", [24] = "sched_yield", [25] = "mremap",
    [26] = "msync", [27] = "mincore", [28] = "madvise", [29] = "shmget",
    [30] = "shmat", [31] = "shmctl", [32] = "dup", [33] = "dup2",
    [34] = "pause", [35] = "nanosleep", [36] = "getitimer", [37] = "alarm",
    [38] = "setitimer", [39] = "getpid", [40] = "sendfile", [41] = "socket",
    [42] = "connect", [43] = "accept", [44] = "sendto", [45] = "recvfrom",
    [46] = "sendmsg", [47] = "recvmsg", [48] = "shutdown", [49] = "bind",
    [50] = "listen", [51] = "getsockname", [52] = "getpeername",
    [53] = "socketpair", [54] = "setsockopt", [55] = "getsockopt",
    [56] = "clone", [57] = "fork", [58] = "vfork", [59] = "execve",
    [60] = "exit", [61] = "wait4", [62] = "kill", [63] = "uname",
    [64] = "semget", [65] = "semop", [66] = "semctl", [67] = "shmdt",
    [68] = "msgget", [69] = "msgsnd", [70] = "msgrcv", [71] = "msgctl",
    [72] = "fcntl", [73] = "flock", [74] = "fsync", [75] = "fdatasync",
    [76] = "truncate", [77] = "ftruncate", [78] = "getdents",
    [79] = "getcwd", [80] = "chdir", [81] = "fchdir", [82] = "rename",
    [83] = "mkdir", [84] = "rmdir", [85] = "creat", [86] = "link",
    [87] = "unlink", [88] = "symlink", [89] = "readlink", [90] = "chmod",
    [91] = "fchmod", [92] = "chown", [93] = "fchown", [94] = "lchown",
    [95] = "umask", [96] = "gettimeofday", [97] = "getrlimit",
    [98] = "getrusage", [99] = "sysinfo", [100] = "times",
    [101] = "ptrace", [102] = "getuid", [103] = "syslog",
    [104] = "getgid", [105] = "setuid", [106] = "setgid",
    [107] = "geteuid", [108] = "getegid", [109] = "setpgid",
    [110] = "getppid", [111] = "getpgrp", [112] = "setsid",
    [113] = "setreuid", [114] = "setregid", [115] = "getgroups",
    [116] = "setgroups", [117] = "setresuid", [118] = "getresuid",
    [119] = "setresgid", [120] = "getresgid", [121] = "getpgid",
    [122] = "setfsuid", [123] = "setfsgid", [124] = "getsid",
    [125] = "capget", [126] = "capset", [127] = "rt_sigpending",
    [128] = "rt_sigtimedwait", [129] = "rt_sigqueueinfo",
    [130] = "rt_sigsuspend", [131] = "sigaltstack", [132] = "utime",
    [133] = "mknod", [134] = "uselib", [135] = "personality",
    [136] = "ustat", [137] = "statfs", [138] = "fstatfs", [139] = "sysfs",
    [140] = "getpriority", [141] = "setpriority",
    [142] = "sched_setparam", [143] = "sched_getparam",
    [144] = "sched_setscheduler", [145] = "sched_getscheduler",
    [146] = "sched_get_priority_max", [147] = "sched_get_priority_min",
    [148] = "sched_rr_get_interval", [149] = "mlock", [150] = "munlock",
    [151] = "mlockall", [152] = "munlockall", [153] = "vhangup",
    [154] = "modify_ldt", [155] = "pivot_root", [156] = "_sysctl",
    [157] = "prctl", [158] = "arch_prctl", [159] = "adjtimex",
    [160] = "setrlimit", [161] = "chroot", [162] = "sync",
    [163] = "acct", [164] = "settimeofday", [165] = "mount",
    [166] = "umount2", [167] = "swapon", [168] = "swapoff",
    [169] = "reboot", [170] = "sethostname", [171] = "setdomainname",
    [172] = "iopl", [173] = "ioperm", [174] = "create_module",
    [175] = "init_module", [176] = "delete_module",
    [177] = "get_kernel_syms", [178] = "query_module",
    [179] = "quotactl", [180] = "nfsservctl", [181] = "getpmsg",
    [182] = "putpmsg", [183] = "afs_syscall", [184] = "tuxcall",
    [185] = "security", [186] = "gettid", [187] = "readahead",
    [188] = "setxattr", [189] = "lsetxattr", [190] = "fsetxattr",
    [191] = "getxattr", [192] = "lgetxattr", [193] = "fgetxattr",
    [194] = "listxattr", [195] = "llistxattr", [196] = "flistxattr",
    [197] = "removexattr", [198] = "lremovexattr", [199] = "fremovexattr",
    [200] = "tkill", [201] = "time", [202] = "futex",
    [203] = "sched_setaffinity", [204] = "sched_getaffinity",
    [205] = "set_thread_area", [206] = "io_setup", [207] = "io_destroy",
    [208] = "io_getevents", [209] = "io_submit", [210] = "io_cancel",
    [211] = "get_thread_area", [212] = "lookup_dcookie",
    [213] = "epoll_create", [214] = "epoll_ctl_old",
    [215] = "epoll_wait_old", [216] = "remap_file_pages",
    [217] = "getdents64", [218] = "set_tid_address",
    [219] = "restart_syscall", [220] = "semtimedop", [221] = "fadvise64",
    [222] = "timer_create", [223] = "timer_settime",
    [224] = "timer_gettime", [225] = "timer_getoverrun",
    [226] = "timer_delete", [227] = "clock_settime",
    [228] = "clock_gettime", [229] = "clock_getres",
    [230] = "clock_nanosleep", [231] = "exit_group", [232] = "epoll_wait",
    [233] = "epoll_ctl", [234] = "tgkill", [235] = "utimes",
    [236] = "vserver", [237] = "mbind", [238] = "set_mempolicy",
    [239] = "get_mempolicy", [240] = "mq_open", [241] = "mq_unlink",
    [242] = "mq_timedsend", [243] = "mq_timedreceive", [244] = "mq_notify",
    [245] = "mq_getsetattr", [246] = "kexec_load", [247] = "waitid",
    [248] = "add_key", [249] = "request_key", [250] = "keyctl",
    [251] = "ioprio_set", [252] = "ioprio_get", [253] = "inotify_init",
    [254] = "inotify_add_watch", [255] = "inotify_rm_watch",
    [256] = "migrate_pages", [257] = "openat", [258] = "mkdirat",
    [259] = "mknodat", [260] = "fchownat", [261] = "futimesat",
    [262] = "newfstatat", [263] = "unlinkat", [264] = "renameat",
    [265] = "linkat", [266] = "symlinkat", [267] = "readlinkat",
    [268] = "fchmodat", [269] = "faccessat", [270] = "pselect6",
    [271] = "ppoll", [272] = "unshare", [273] = "set_robust_list",
    [274] = "get_robust_list", [275] = "splice", [276] = "tee",
    [277] = "sync_file_range", [278] = "vmsplice", [279] = "move_pages",
    [280] = "utimensat", [281] = "epoll_pwait", [282] = "signalfd",
    [283] = "timerfd_create", [284] = "eventfd", [285] = "fallocate",
    [286] = "timerfd_settime", [287] = "timerfd_gettime",
    [288] = "accept4", [289] = "signalfd4", [290] = "eventfd2",
    [291] = "epoll_create1", [292] = "dup3", [293] = "pipe2",
    [294] = "inotify_init1", [295] = "preadv", [296] = "pwritev",
    [297] = "rt_tgsigqueueinfo", [298] = "perf_event_open",
    [299] = "recvmmsg", [300] = "fanotify_init", [301] = "fanotify_mark",
    [302] = "prlimit64", [303] = "name_to_handle_at",
    [304] = "open_by_handle_at", [305] = "clock_adjtime",
    [306] = "syncfs", [307] = "sendmmsg", [308] = "setns",
    [309] = "getcpu", [310] = "process_vm_readv",
    [311] = "process_vm_writev", [312] = "kcmp", [313] = "finit_module",
    [314] = "sched_setattr", [315] = "sched_getattr",
    [316] = "renameat2", [317] = "seccomp", [318] = "getrandom",
    [319] = "memfd_create", [320] = "kexec_file_load", [321] = "bpf",
    [322] = "execveat", [323] = "userfaultfd", [324] = "membarrier",
    [325] = "mlock2", [326] = "copy_file_range", [327] = "preadv2",
    [328] = "pwritev2",
};

#define NUM_SYSCALL_NAMES (sizeof(syscall_names) / sizeof(syscall_names[0]))
#define MAX_SYSCALL_KINDS 128
#define MAX_SNOOP_ARGS 64

typedef struct {
    long syscall_num;
    int count;
    double total_time;
    int first_order; /* req 6 tie-break: order of first occurrence */
} syscall_stat_t;

static syscall_stat_t stats[MAX_SYSCALL_KINDS];
static int num_stats = 0;

static double timespec_diff(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

/* req 4: accumulate calls + time per syscall number */
static void record_syscall(long num, double elapsed)
{
    for (int i = 0; i < num_stats; i++) {
        if (stats[i].syscall_num == num) {
            stats[i].count++;
            stats[i].total_time += elapsed;
            return;
        }
    }
    if (num_stats < MAX_SYSCALL_KINDS) {
        stats[num_stats].syscall_num = num;
        stats[num_stats].count = 1;
        stats[num_stats].total_time = elapsed;
        stats[num_stats].first_order = num_stats;
        num_stats++;
    }
}

/* req 6: sort by call count descending, ties broken by order of
 * first occurrence. */
static int compare_stats(const void *a, const void *b)
{
    const syscall_stat_t *sa = a;
    const syscall_stat_t *sb = b;
    if (sb->count != sa->count) {
        return sb->count - sa->count;
    }
    return sa->first_order - sb->first_order;
}

static const char *syscall_name(long num)
{
    if (num >= 0 && (size_t)num < NUM_SYSCALL_NAMES && syscall_names[num] != NULL) {
        return syscall_names[num];
    }
    return NULL; /* req 7: caller falls back to "syscall_N" */
}

static void print_summary(void)
{
    qsort(stats, num_stats, sizeof(syscall_stat_t), compare_stats);

    printf("%-16s%-8s%s\n", "syscall", "calls", "time");
    for (int i = 0; i < num_stats; i++) {
        char name_buf[32];
        const char *name = syscall_name(stats[i].syscall_num);
        if (name == NULL) {
            snprintf(name_buf, sizeof(name_buf), "syscall_%ld", stats[i].syscall_num);
            name = name_buf;
        }
        printf("%-16s%-8d%.3fs\n", name, stats[i].count, stats[i].total_time);
    }
    fflush(stdout);
}

/* req 3/4: drive the tracee via PTRACE_SYSCALL, alternating entry/exit
 * stops for the same syscall, timing each pair, until it exits. */
static void trace_loop(pid_t pid)
{
    int in_syscall = 0;
    long current_num = -1;
    struct timespec entry_time = {0};

    for (;;) {
        if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) < 0) {
            break;
        }

        int status;
        if (waitpid(pid, &status, 0) < 0) {
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            /* The tracee died inside a syscall we never got an
             * exit-stop for (e.g. exit_group tears the process down
             * as part of the call itself) -- still count it, timed
             * up to this point, rather than silently dropping it. */
            if (in_syscall) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                record_syscall(current_num, timespec_diff(entry_time, now));
            }
            break;
        }

        if (!WIFSTOPPED(status)) {
            continue;
        }

        if (!in_syscall) {
            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == 0) {
                current_num = (long)regs.orig_rax;
                clock_gettime(CLOCK_MONOTONIC, &entry_time);
                in_syscall = 1;
            }
        } else {
            struct timespec exit_time;
            clock_gettime(CLOCK_MONOTONIC, &exit_time);
            record_syscall(current_num, timespec_diff(entry_time, exit_time));
            in_syscall = 0;
        }
    }
}

static int all_digits(const char *s)
{
    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (const char *c = s; *c != '\0'; c++) {
        if (*c < '0' || *c > '9') {
            return 0;
        }
    }
    return 1;
}

void snoop_execute(int argc, char **argv)
{
    num_stats = 0; /* fresh table each invocation */

    if (argc >= 3 && strcmp(argv[1], "-p") == 0) {
        /* req 2: attach to a running process */
        if (!all_digits(argv[2])) {
            printf("snoop: no such process\n");
            return;
        }
        pid_t pid = (pid_t)atoi(argv[2]);

        if (kill(pid, 0) < 0) {
            printf("snoop: no such process\n"); /* req 8 */
            return;
        }
        if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
            /* attach can fail (e.g. permission/ptrace_scope) even
             * though the pid exists -- only error text the spec
             * defines for a bad target is this one. */
            printf("snoop: no such process\n");
            return;
        }

        int status;
        waitpid(pid, &status, 0); /* reap the attach-induced stop */

        trace_loop(pid);
        print_summary();
        return;
    }

    if (argc < 2) {
        printf("snoop: command not found\n");
        return;
    }

    /* req 1: fork, PTRACE_TRACEME in the child, then execve the
     * command. */
    pid_t child = fork();
    if (child < 0) {
        perror("cshell: fork");
        return;
    }

    if (child == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);

        /* execvp() requires a NULL-terminated argv, but the argv/argc
         * pair we were handed comes from main.c's build_argv(), which
         * only fills argv[0..argc-1] -- argv[argc] is uninitialized
         * garbage, not NULL. Passing &argv[1] straight to execvp()
         * would read that garbage as one more argument pointer
         * (undefined behavior, and in practice makes execvp() fail
         * for reasons that have nothing to do with the command
         * actually existing). Copy into a properly terminated array
         * instead. */
        char *exec_argv[MAX_SNOOP_ARGS + 1];
        int n = argc - 1;
        if (n > MAX_SNOOP_ARGS) {
            n = MAX_SNOOP_ARGS;
        }
        for (int i = 0; i < n; i++) {
            exec_argv[i] = argv[1 + i];
        }
        exec_argv[n] = NULL;

        execvp(exec_argv[0], exec_argv);
        /* only reached if execvp() failed */
        _exit(127);
    }

    int status;
    waitpid(child, &status, 0); /* the post-exec SIGTRAP, or an early exit */

    if (WIFEXITED(status)) {
        /* execvp() never succeeded -- no PTRACE_TRACEME trap ever
         * fired, the child just ran straight to _exit(127). */
        printf("snoop: command not found\n"); /* req 9 */
        return;
    }

    trace_loop(child);
    print_summary();
}