#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int    fd;
    char  *buf;
    size_t cap;
    size_t len;
    size_t start;
    bool   eof;
} reader_t;

int   reader_init(reader_t *r, int fd);
char *reader_next_line(reader_t *r);
void  reader_free(reader_t *r);

#endif