#ifndef PARSE_H
#define PARSE_H

#include "arraylist.h"
#include "job.h"

typedef enum {
    PARSE_OK,
    PARSE_EMPTY,
    PARSE_ERROR
} parse_status_t;

void           tokenize(const char *line, arraylist_t *tokens);
parse_status_t parse_line(const char *line, job_t *job);
void           free_job(job_t *job);

#endif