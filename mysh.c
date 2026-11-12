#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **data;
    int    len;
    int    cap;
} arraylist_t;

typedef struct {
    int    fd;
    char  *buf;
    size_t cap;
    size_t len;
    size_t start;
    bool   eof;
} reader_t;

typedef struct {
    char **argv;
    int    argc;
    char  *infile;
    char  *outfile;
    bool   append;
} subcmd_t;

typedef struct {
    subcmd_t *cmds;
    int       ncmds;
} job_t;

typedef enum {
    PARSE_OK,
    PARSE_EMPTY,
    PARSE_ERROR
} parse_status_t;

typedef struct {
    int  exit_code;
    int  signal;
    bool exit_requested;
} result_t;

static char *get_cwd(void);

static void al_init(arraylist_t *l)
{
    l->data = NULL;
    l->len = 0;
    l->cap = 0;
}

static void al_push(arraylist_t *l, char *s)
{
    if (l->len == l->cap) {
        int newcap = l->cap ? l->cap * 2 : 8;
        char **nd = realloc(l->data, (size_t)newcap * sizeof(char *));
        if (!nd) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        l->data = nd;
        l->cap = newcap;
    }
    l->data[l->len++] = s;
}

static char **al_take(arraylist_t *l, int *len_out)
{
    int n = l->len;
    al_push(l, NULL);
    char **out = l->data;
    if (len_out)
        *len_out = n;
    al_init(l);
    return out;
}

static void al_free(arraylist_t *l)
{
    for (int i = 0; i < l->len; i++)
        free(l->data[i]);
    free(l->data);
    al_init(l);
}

#ifndef INITIAL_CAP
#define INITIAL_CAP 1024
#endif

static int reader_init(reader_t *r, int fd)
{
    r->fd = fd;
    r->cap = INITIAL_CAP;
    r->len = 0;
    r->start = 0;
    r->eof = false;
    r->buf = malloc(r->cap);
    return r->buf ? 0 : -1;
}

static void reader_free(reader_t *r)
{
    free(r->buf);
    r->buf = NULL;
}

static char *reader_next_line(reader_t *r)
{
    for (;;) {
        char *nl = memchr(r->buf + r->start, '\n', r->len - r->start);
        if (nl) {
            char *line = r->buf + r->start;
            *nl = '\0';
            r->start = (size_t)(nl - r->buf) + 1;
            return line;
        }

        if (r->eof) {
            if (r->start < r->len) {
                char *line = r->buf + r->start;
                r->buf[r->len] = '\0';
                r->start = r->len;
                return line;
            }
            return NULL;
        }

        if (r->start > 0) {
            memmove(r->buf, r->buf + r->start, r->len - r->start);
            r->len -= r->start;
            r->start = 0;
        }

        if (r->len + 1 >= r->cap) {
            size_t newcap = r->cap * 2;
            char *nb = realloc(r->buf, newcap);
            if (!nb) {
                perror("realloc");
                r->eof = true;
                continue;
            }
            r->buf = nb;
            r->cap = newcap;
        }

        ssize_t n = read(r->fd, r->buf + r->len, r->cap - r->len - 1);
        if (n > 0) {
            r->len += (size_t)n;
        } else if (n == 0) {
            r->eof = true;
        } else if (errno != EINTR) {
            perror("read");
            r->eof = true;
        }
    }
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static char *join(const char *dir, size_t dlen, const char *name)
{
    size_t nlen = strlen(name);
    char *s = malloc(dlen + nlen + 1);
    if (!s)
        return NULL;
    memcpy(s, dir, dlen);
    memcpy(s + dlen, name, nlen + 1);
    return s;
}

static void glob_expand(const char *pattern, arraylist_t *out)
{
    const char *slash = strrchr(pattern, '/');
    const char *file = slash ? slash + 1 : pattern;
    const char *star = strchr(file, '*');

    if (!star) {
        al_push(out, strdup(pattern));
        return;
    }

    size_t dlen = slash ? (size_t)(file - pattern) : 0;
    char *dirname = slash ? strndup(pattern, dlen) : strdup(".");
    const char *prefix = file;
    size_t plen = (size_t)(star - file);
    const char *suffix = star + 1;
    size_t slen = strlen(suffix);

    DIR *d = opendir(dirname);
    free(dirname);
    if (!d) {
        al_push(out, strdup(pattern));
        return;
    }

    arraylist_t matches;
    al_init(&matches);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t nlen = strlen(name);

        if (name[0] == '.' && plen == 0)
            continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (nlen < plen + slen)
            continue;
        if (strncmp(name, prefix, plen) != 0)
            continue;
        if (strcmp(name + nlen - slen, suffix) != 0)
            continue;

        al_push(&matches, join(pattern, dlen, name));
    }
    closedir(d);

    if (matches.len == 0) {
        al_free(&matches);
        al_push(out, strdup(pattern));
        return;
    }

    qsort(matches.data, (size_t)matches.len, sizeof(char *), cmp_str);
    for (int i = 0; i < matches.len; i++)
        al_push(out, matches.data[i]);
    free(matches.data);
}

static int is_space(char c)
{
    return isspace((unsigned char)c);
}

static int is_special(char c)
{
    return c == '<' || c == '>' || c == '|' || c == '#';
}

static void tokenize(const char *line, arraylist_t *tokens)
{
    size_t i = 0;
    for (;;) {
        while (is_space(line[i]))
            i++;

        char c = line[i];
        if (c == '\0' || c == '#')
            return;

        if (c == '<' || c == '|') {
            al_push(tokens, strndup(&line[i], 1));
            i++;
        } else if (c == '>') {
            size_t n = (line[i + 1] == '>') ? 2 : 1;
            al_push(tokens, strndup(&line[i], n));
            i += n;
        } else {
            size_t start = i;
            while (line[i] && !is_space(line[i]) && !is_special(line[i]))
                i++;
            al_push(tokens, strndup(&line[start], i - start));
        }
    }
}

static int is_redirect(const char *t)
{
    return strcmp(t, "<") == 0 || strcmp(t, ">") == 0 || strcmp(t, ">>") == 0;
}

static int is_operator(const char *t)
{
    return is_redirect(t) || strcmp(t, "|") == 0;
}

static void push_cmd(job_t *job, int *cap, subcmd_t cmd)
{
    if (job->ncmds == *cap) {
        *cap = *cap ? *cap * 2 : 4;
        subcmd_t *nc = realloc(job->cmds, (size_t)*cap * sizeof(subcmd_t));
        if (!nc) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        job->cmds = nc;
    }
    job->cmds[job->ncmds++] = cmd;
}

static void free_job(job_t *job)
{
    for (int i = 0; i < job->ncmds; i++) {
        subcmd_t *c = &job->cmds[i];
        for (int j = 0; j < c->argc; j++)
            free(c->argv[j]);
        free(c->argv);
        free(c->infile);
        free(c->outfile);
    }
    free(job->cmds);
    job->cmds = NULL;
    job->ncmds = 0;
}

static parse_status_t parse_line(const char *line, job_t *job)
{
    job->cmds = NULL;
    job->ncmds = 0;

    arraylist_t toks;
    al_init(&toks);
    tokenize(line, &toks);

    if (toks.len == 0) {
        al_free(&toks);
        return PARSE_EMPTY;
    }

    int cap = 0;
    arraylist_t args;
    al_init(&args);
    subcmd_t cur = {NULL, 0, NULL, NULL, false};
    const char *err = NULL;

    for (int i = 0; i < toks.len && !err; i++) {
        const char *t = toks.data[i];

        if (strcmp(t, "|") == 0) {
            if (args.len == 0) {
                err = "missing command before '|'";
                break;
            }
            cur.argv = al_take(&args, &cur.argc);
            push_cmd(job, &cap, cur);
            cur = (subcmd_t){NULL, 0, NULL, NULL, false};
        } else if (is_redirect(t)) {
            if (i + 1 >= toks.len || is_operator(toks.data[i + 1])) {
                err = "expected file name after redirection";
                break;
            }
            char *file = strdup(toks.data[++i]);
            if (strcmp(t, "<") == 0) {
                free(cur.infile);
                cur.infile = file;
            } else {
                free(cur.outfile);
                cur.outfile = file;
                cur.append = (strcmp(t, ">>") == 0);
            }
        } else {
            glob_expand(t, &args);
        }
    }

    if (!err && args.len == 0)
        err = (job->ncmds > 0) ? "missing command after '|'" : "missing command";

    if (err) {
        dprintf(STDERR_FILENO, "mysh: syntax error: %s\n", err);
        al_free(&args);
        free(cur.infile);
        free(cur.outfile);
        free_job(job);
        al_free(&toks);
        return PARSE_ERROR;
    }

    cur.argv = al_take(&args, &cur.argc);
    push_cmd(job, &cap, cur);
    al_free(&toks);
    return PARSE_OK;
}

static bool is_builtin(const char *name)
{
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0
        || strcmp(name, "which") == 0 || strcmp(name, "exit") == 0;
}

static char *find_program(const char *name)
{
    static const char *dirs[] = {"/usr/local/bin", "/usr/bin", "/bin"};

    if (strchr(name, '/'))
        return access(name, X_OK) == 0 ? strdup(name) : NULL;

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        size_t len = strlen(dirs[i]) + strlen(name) + 2;
        char *path = malloc(len);
        if (!path)
            return NULL;
        snprintf(path, len, "%s/%s", dirs[i], name);
        if (access(path, X_OK) == 0)
            return path;
        free(path);
    }
    return NULL;
}

static int open_input(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        dprintf(STDERR_FILENO, "%s: %s\n", path, strerror(errno));
    return fd;
}

static int open_output(const subcmd_t *cmd)
{
    int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
    int fd = open(cmd->outfile, flags | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0)
        dprintf(STDERR_FILENO, "%s: %s\n", cmd->outfile, strerror(errno));
    return fd;
}

static int apply_redirections(const subcmd_t *cmd)
{
    if (cmd->infile) {
        int fd = open_input(cmd->infile);
        if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
            if (fd >= 0)
                close(fd);
            return -1;
        }
        close(fd);
    }
    if (cmd->outfile) {
        int fd = open_output(cmd);
        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) {
            if (fd >= 0)
                close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int builtin_which(const subcmd_t *cmd)
{
    int status = cmd->argc > 1 ? 0 : 1;
    for (int i = 1; i < cmd->argc; i++) {
        char *path = find_program(cmd->argv[i]);
        if (path) {
            dprintf(STDOUT_FILENO, "%s\n", path);
            free(path);
        } else {
            status = 1;
        }
    }
    return status;
}

static int run_builtin(const subcmd_t *cmd)
{
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argc > 2) {
            dprintf(STDERR_FILENO, "cd: too many arguments\n");
            return 1;
        }
        const char *dir = cmd->argc == 1 ? getenv("HOME") : cmd->argv[1];
        if (!dir || chdir(dir) < 0) {
            dprintf(STDERR_FILENO, "cd: %s: %s\n",
                    dir ? dir : "HOME is not set", strerror(errno));
            return 1;
        }
        return 0;
    }
    if (strcmp(cmd->argv[0], "pwd") == 0) {
        char *cwd = getcwd(NULL, 0);
        if (!cwd) {
            dprintf(STDERR_FILENO, "pwd: %s\n", strerror(errno));
            return 1;
        }
        dprintf(STDOUT_FILENO, "%s\n", cwd);
        free(cwd);
        return 0;
    }
    if (strcmp(cmd->argv[0], "which") == 0)
        return builtin_which(cmd);
    return 0;
}

static void run_child(const subcmd_t *cmd, bool interactive, int input_fd,
                      int output_fd)
{
    if (input_fd >= 0 && dup2(input_fd, STDIN_FILENO) < 0)
        _exit(EXIT_FAILURE);
    if (output_fd >= 0 && dup2(output_fd, STDOUT_FILENO) < 0)
        _exit(EXIT_FAILURE);

    if (input_fd >= 0)
        close(input_fd);
    if (output_fd >= 0 && output_fd != input_fd)
        close(output_fd);

    if (!interactive && input_fd < 0 && !cmd->infile) {
        int nullfd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (nullfd < 0 || dup2(nullfd, STDIN_FILENO) < 0)
            _exit(EXIT_FAILURE);
        close(nullfd);
    }
    if (apply_redirections(cmd) < 0)
        _exit(EXIT_FAILURE);

    if (is_builtin(cmd->argv[0]))
        _exit(run_builtin(cmd));

    char *path = find_program(cmd->argv[0]);
    if (!path) {
        dprintf(STDERR_FILENO, "mysh: %s: command not found\n", cmd->argv[0]);
        _exit(127);
    }
    execv(path, cmd->argv);
    dprintf(STDERR_FILENO, "mysh: %s: %s\n", cmd->argv[0], strerror(errno));
    free(path);
    _exit(126);
}

static result_t run_parent_builtin(const subcmd_t *cmd)
{
    result_t res = {0, 0, strcmp(cmd->argv[0], "exit") == 0};
    int saved_in = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    if (saved_in < 0 || saved_out < 0 || apply_redirections(cmd) < 0) {
        res.exit_code = 1;
    } else {
        res.exit_code = run_builtin(cmd);
    }
    if (saved_in >= 0) {
        dup2(saved_in, STDIN_FILENO);
        close(saved_in);
    }
    if (saved_out >= 0) {
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    return res;
}

static result_t run_job(const job_t *job, bool interactive)
{
    result_t res = {0, 0, false};
    if (job->ncmds == 1 && is_builtin(job->cmds[0].argv[0]))
        return run_parent_builtin(&job->cmds[0]);

    int n = job->ncmds;
    int (*pipes)[2] = n > 1 ? calloc((size_t)n - 1, sizeof(*pipes)) : NULL;
    pid_t *pids = calloc((size_t)n, sizeof(*pids));
    if (!pids || (n > 1 && !pipes)) {
        perror("calloc");
        free(pipes);
        free(pids);
        res.exit_code = 1;
        return res;
    }
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(pipes);
            free(pids);
            res.exit_code = 1;
            return res;
        }
    }

    for (int i = 0; i < n; i++) {
        int in = i > 0 ? pipes[i - 1][0] : -1;
        int out = i < n - 1 ? pipes[i][1] : -1;
        pids[i] = fork();
        if (pids[i] == 0) {
            for (int j = 0; j < n - 1; j++) {
                if (pipes[j][0] != in)
                    close(pipes[j][0]);
                if (pipes[j][1] != out)
                    close(pipes[j][1]);
            }
            run_child(&job->cmds[i], interactive, in, out);
        }
        if (pids[i] < 0)
            dprintf(STDERR_FILENO, "mysh: fork: %s\n", strerror(errno));
    }
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < n; i++) {
        int status;
        if (pids[i] <= 0 || waitpid(pids[i], &status, 0) < 0) {
            res.exit_code = 1;
            continue;
        }
        if (i == n - 1) {
            if (WIFEXITED(status))
                res.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                res.signal = WTERMSIG(status);
        }
    }
    for (int i = 0; i < n; i++)
        if (strcmp(job->cmds[i].argv[0], "exit") == 0)
            res.exit_requested = true;
    free(pipes);
    free(pids);
    return res;
}

static char *get_cwd(void)
{
    size_t size = 256;
    for (;;) {
        char *buf = malloc(size);
        if (!buf)
            return NULL;
        if (getcwd(buf, size))
            return buf;
        free(buf);
        if (errno != ERANGE)
            return NULL;
        size *= 2;
    }
}

static void print_prompt(void)
{
    char *cwd = get_cwd();
    if (!cwd) {
        dprintf(STDOUT_FILENO, "$ ");
        return;
    }

    const char *home = getenv("HOME");
    size_t hlen = home ? strlen(home) : 0;

    if (hlen > 0 && strncmp(cwd, home, hlen) == 0
        && (cwd[hlen] == '\0' || cwd[hlen] == '/')) {
        dprintf(STDOUT_FILENO, "~%s$ ", cwd + hlen);
    } else {
        dprintf(STDOUT_FILENO, "%s$ ", cwd);
    }
    free(cwd);
}

static void report_status(result_t res)
{
    if (res.signal != 0)
        dprintf(STDOUT_FILENO, "Terminated by signal: %s\n", strsignal(res.signal));
    else if (res.exit_code != 0)
        dprintf(STDOUT_FILENO, "Exited with status %d\n", res.exit_code);
}

int main(int argc, char **argv)
{
    int fd = STDIN_FILENO;

    if (argc > 2)
        dprintf(STDERR_FILENO, "mysh: ignoring extra arguments after %s\n", argv[1]);
    if (argc >= 2) {
        fd = open(argv[1], O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }

    bool interactive = (argc == 1) && isatty(STDIN_FILENO);
    bool hit_eof = false;

    reader_t reader;
    if (reader_init(&reader, fd) < 0) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    if (interactive)
        dprintf(STDOUT_FILENO, "Entering shell\n");

    for (;;) {
        if (interactive)
            print_prompt();

        char *line = reader_next_line(&reader);
        if (!line) {
            hit_eof = true;
            break;
        }

        job_t job;
        parse_status_t ps = parse_line(line, &job);
        if (ps == PARSE_EMPTY)
            continue;
        if (ps == PARSE_ERROR) {
            if (interactive)
                report_status((result_t){1, 0, false});
            continue;
        }

        result_t res = run_job(&job, interactive);
        free_job(&job);

        if (res.exit_requested)
            break;
        if (interactive)
            report_status(res);
    }

    if (interactive)
        dprintf(STDOUT_FILENO, "%sExiting shell\n", hit_eof ? "\n" : "");

    reader_free(&reader);
    if (fd != STDIN_FILENO)
        close(fd);
    return EXIT_SUCCESS;
}