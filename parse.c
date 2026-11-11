#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "glob.h"
#include "parse.h"

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_special(char c)
{
    return c == '<' || c == '>' || c == '|' || c == '#';
}

void tokenize(const char *line, arraylist_t *tokens)
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

void free_job(job_t *job)
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

parse_status_t parse_line(const char *line, job_t *job)
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