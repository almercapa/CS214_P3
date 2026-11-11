#ifndef JOB_H
#define JOB_H

#include <stdbool.h>

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

#endif