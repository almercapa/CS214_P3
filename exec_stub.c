#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "exec.h"

result_t run_job(const job_t *job, bool interactive)
{
    (void)interactive;
    result_t res = {0, 0, false};

    for (int i = 0; i < job->ncmds; i++) {
        const subcmd_t *c = &job->cmds[i];
        dprintf(STDOUT_FILENO, "cmd %d: argc=%d argv=", i, c->argc);
        for (int j = 0; c->argv[j]; j++)
            dprintf(STDOUT_FILENO, "[%s]", c->argv[j]);
        if (c->infile)
            dprintf(STDOUT_FILENO, " in=[%s]", c->infile);
        if (c->outfile)
            dprintf(STDOUT_FILENO, " %s=[%s]", c->append ? "append" : "out", c->outfile);
        dprintf(STDOUT_FILENO, "\n");

        if (strcmp(c->argv[0], "exit") == 0)
            res.exit_requested = true;
        if (strcmp(c->argv[0], "false") == 0)
            res.exit_code = 1;
    }
    return res;
}