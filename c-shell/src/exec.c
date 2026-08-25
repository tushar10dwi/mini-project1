#include "exec.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_REDIR 32
#define IO_BUF_SIZE 65536

typedef struct {
    char *filename;
    int append;
} output_redir_t;

static int is_executable_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    return access(path, X_OK) == 0;
}

static int resolve_executable(const char *raw_name, char *out, size_t out_size, char **bare_name_out)
{
    int skip_cwd = 0;
    const char *name = raw_name;

    if (name[0] == '%') {
        skip_cwd = 1;
        name++;
    }
    *bare_name_out = strdup(name);

    if (strchr(name, '/') != NULL) {
        if (is_executable_file(name)) {
            snprintf(out, out_size, "%s", name);
            return 1;
        }
        return 0;
    }

    if (!skip_cwd) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            char candidate[PATH_MAX + 256];
            snprintf(candidate, sizeof(candidate), "%s/%s", cwd, name);
            if (is_executable_file(candidate)) {
                snprintf(out, out_size, "%s", candidate);
                return 1;
            }
        }
    }

    const char *path_env = getenv("PATH");
    if (path_env != NULL) {
        char *copy = strdup(path_env);
        if (copy != NULL) {
            char *seg = copy;
            for (;;) {
                char *colon = strchr(seg, ':');
                if (colon != NULL) {
                    *colon = '\0';
                }

                const char *dir = (seg[0] == '\0') ? "." : seg;
                char resolved_dir[PATH_MAX];
                if (realpath(dir, resolved_dir) != NULL) {
                    char candidate[PATH_MAX + 256];
                    snprintf(candidate, sizeof(candidate), "%s/%s", resolved_dir, name);
                    if (is_executable_file(candidate)) {
                        snprintf(out, out_size, "%s", candidate);
                        free(copy);
                        return 1;
                    }
                }

                if (colon == NULL) {
                    break;
                }
                seg = colon + 1;
            }
            free(copy);
        }
    }

    return 0;
}

static int parse_redirections(int argc, char **argv, char **clean_argv, int *clean_argc, char **input_files, int *n_input, output_redir_t *output_files, int *n_output)
{
    *clean_argc = 0;
    *n_input = 0;
    *n_output = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "<") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cshell: missing filename after '<'\n");
                return -1;
            }
            if (*n_input >= MAX_REDIR) {
                fprintf(stderr, "cshell: too many input redirections\n");
                return -1;
            }
            input_files[(*n_input)++] = argv[++i];
        } else if (strcmp(argv[i], ">>") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cshell: missing filename after '>>'\n");
                return -1;
            }
            if (*n_output >= MAX_REDIR) {
                fprintf(stderr, "cshell: too many output redirections\n");
                return -1;
            }
            output_files[*n_output].filename = argv[++i];
            output_files[*n_output].append = 1;
            (*n_output)++;
        } else if (strcmp(argv[i], ">") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "cshell: missing filename after '>'\n");
                return -1;
            }
            if (*n_output >= MAX_REDIR) {
                fprintf(stderr, "cshell: too many output redirections\n");
                return -1;
            }
            output_files[*n_output].filename = argv[++i];
            output_files[*n_output].append = 0;
            (*n_output)++;
        } else {
            clean_argv[(*clean_argc)++] = argv[i];
        }
    }
    clean_argv[*clean_argc] = NULL;
    return 0;
}

static int build_concatenated_stdin(int *input_fds, int n_input)
{
    FILE *tmp = tmpfile();
    if (tmp == NULL) {
        perror("cshell: tmpfile");
        for (int k = 0; k < n_input; k++) {
            close(input_fds[k]);
        }
        return -1;
    }

    char buf[IO_BUF_SIZE];
    for (int k = 0; k < n_input; k++) {
        ssize_t r;
        while ((r = read(input_fds[k], buf, sizeof(buf))) > 0) {
            if (fwrite(buf, 1, (size_t)r, tmp) != (size_t)r) {
                perror("cshell: write to temp file");
                close(input_fds[k]);
                fclose(tmp);
                return -1;
            }
        }
        close(input_fds[k]);
    }

    fflush(tmp);
    rewind(tmp);

    int fd = dup(fileno(tmp));
    fclose(tmp);
    return fd;
}

static pid_t start_output_tee(int *output_fds, int n_output, int *pipe_write_fd)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("cshell: pipe");
        return -1;
    }

    pid_t tee_pid = fork();
    if (tee_pid < 0) {
        perror("cshell: fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (tee_pid == 0) {
        /* Tee helper: read the pipe, fan out to every output file. */
        close(pipefd[1]);
        char buf[IO_BUF_SIZE];
        ssize_t r;
        while ((r = read(pipefd[0], buf, sizeof(buf))) > 0) {
            for (int k = 0; k < n_output; k++) {
                ssize_t written = 0;
                while (written < r) {
                    ssize_t w = write(output_fds[k], buf + written, (size_t)(r - written));
                    if (w < 0) {
                        break;
                    }
                    written += w;
                }
            }
        }
        close(pipefd[0]);
        for (int k = 0; k < n_output; k++) {
            close(output_fds[k]);
        }
        _exit(0);
    }

    close(pipefd[0]);
    *pipe_write_fd = pipefd[1];
    return tee_pid;
}

int exec_command(int argc, char **argv)
{
    if (argc < 1 || argv[0] == NULL || argv[0][0] == '\0') {
        return -1;
    }

    char *clean_argv[argc + 1];
    int clean_argc = 0;

    char *input_files[MAX_REDIR];
    int n_input = 0;

    output_redir_t output_files[MAX_REDIR];
    int n_output = 0;

    if (parse_redirections(argc, argv, clean_argv, &clean_argc, input_files, &n_input, output_files, &n_output) != 0) {
        return -1;
    }

    if (clean_argc < 1) {
        fprintf(stderr, "cshell: no command specified\n");
        return -1;
    }

    char resolved[PATH_MAX + 256];
    char *bare_name = NULL;
    int found = resolve_executable(clean_argv[0], resolved, sizeof(resolved), &bare_name);

    if (!found) {
        printf("cshell: command not found (%s)\n", bare_name != NULL ? bare_name : clean_argv[0]);
        free(bare_name);
        return -1;
    }

    int input_fds[MAX_REDIR];
    for (int k = 0; k < n_input; k++) {
        input_fds[k] = open(input_files[k], O_RDONLY);
        if (input_fds[k] < 0) {
            printf("cshell: no such file or directory\n");
            for (int j = 0; j < k; j++) {
                close(input_fds[j]);
            }
            free(bare_name);
            return -1;
        }
    }

    int output_fds[MAX_REDIR];
    for (int k = 0; k < n_output; k++) {
        int flags = O_WRONLY | O_CREAT | (output_files[k].append ? O_APPEND : O_TRUNC);
        output_fds[k] = open(output_files[k].filename, flags, 0644);
        if (output_fds[k] < 0) {
            printf("cshell: unable to create file for writing\n");
            for (int j = 0; j < k; j++) {
                close(output_fds[j]);
            }
            for (int j = 0; j < n_input; j++) {
                close(input_fds[j]);
            }
            free(bare_name);
            return -1;
        }
    }

    int stdin_fd = -1; 
    if (n_input == 1) {
        stdin_fd = input_fds[0];
    } else if (n_input > 1) {
        stdin_fd = build_concatenated_stdin(input_fds, n_input);
        if (stdin_fd < 0) {
            for (int j = 0; j < n_output; j++) {
                close(output_fds[j]);
            }
            free(bare_name);
            return -1;
        }
    }

    int need_tee = (n_output > 1);
    int pipe_write_fd = -1;
    pid_t tee_pid = -1;

    if (need_tee) {
        tee_pid = start_output_tee(output_fds, n_output, &pipe_write_fd);
        if (tee_pid < 0) {
            if (stdin_fd != -1) {
                close(stdin_fd);
            }
            for (int j = 0; j < n_output; j++) {
                close(output_fds[j]);
            }
            free(bare_name);
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("cshell: fork");
        if (stdin_fd != -1) {
            close(stdin_fd);
        }
        if (need_tee) {
            close(pipe_write_fd);
        }
        for (int j = 0; j < n_output; j++) {
            close(output_fds[j]);
        }
        free(bare_name);
        return -1;
    }

    if (pid == 0) {
        if (stdin_fd != -1) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }

        if (need_tee) {
            dup2(pipe_write_fd, STDOUT_FILENO);
            close(pipe_write_fd);
        } else if (n_output == 1) {
            dup2(output_fds[0], STDOUT_FILENO);
        }

        for (int k = 0; k < n_output; k++) {
            close(output_fds[k]);
        }

        char **child_argv = malloc((size_t)(clean_argc + 1) * sizeof(char *));
        if (child_argv == NULL) {
            _exit(127);
        }
        child_argv[0] = bare_name;
        for (int i = 1; i < clean_argc; i++) {
            child_argv[i] = clean_argv[i];
        }
        child_argv[clean_argc] = NULL;

        execv(resolved, child_argv);

        fprintf(stderr, "cshell: exec failed for %s: %s\n", resolved, strerror(errno));
        _exit(127);
    }

    if (stdin_fd != -1) {
        close(stdin_fd);
    }
    if (need_tee) {
        close(pipe_write_fd);
    }
    for (int k = 0; k < n_output; k++) {
        close(output_fds[k]);
    }

    int status;
    waitpid(pid, &status, 0);
    if (need_tee) {
        waitpid(tee_pid, NULL, 0);
    }

    free(bare_name);
    return 0;
}