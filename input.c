#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"

#ifndef INITIAL_CAP
#define INITIAL_CAP 1024
#endif

int reader_init(reader_t *r, int fd)
{
    r->fd = fd;
    r->cap = INITIAL_CAP;
    r->len = 0;
    r->start = 0;
    r->eof = false;
    r->buf = malloc(r->cap);
    return r->buf ? 0 : -1;
}

void reader_free(reader_t *r)
{
    free(r->buf);
    r->buf = NULL;
}

char *reader_next_line(reader_t *r)
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