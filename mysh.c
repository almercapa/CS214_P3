#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "exec.h"
#include "input.h"
#include "parse.h"

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

    if (argc > 2) {
        dprintf(STDERR_FILENO, "usage: %s [script]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        fd = open(argv[1], O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }

    bool interactive = (argc == 1) && isatty(STDIN_FILENO);

    reader_t reader;
    if (reader_init(&reader, fd) < 0) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    if (interactive)
        dprintf(STDOUT_FILENO, " Entering shell.\n");

    for (;;) {
        if (interactive)
            print_prompt();

        char *line = reader_next_line(&reader);
        if (!line)
            break;

        job_t job;
        parse_status_t ps = parse_line(line, &job);
        if (ps != PARSE_OK)
            continue;

        result_t res = run_job(&job, interactive);
        free_job(&job);

        if (res.exit_requested)
            break;
        if (interactive)
            report_status(res);
    }

    if (interactive)
        dprintf(STDOUT_FILENO, "Exiting shell.\n");

    reader_free(&reader);
    if (fd != STDIN_FILENO)
        close(fd);
    return EXIT_SUCCESS;
}