#ifndef EXEC_H
#define EXEC_H

#include <stdbool.h>

#include "job.h"

typedef struct {
    int  exit_code;
    int  signal;
    bool exit_requested;
} result_t;

result_t run_job(const job_t *job, bool interactive);

#endif