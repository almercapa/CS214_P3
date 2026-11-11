#include <string.h>

#include "glob.h"

void glob_expand(const char *pattern, arraylist_t *out)
{
    al_push(out, strdup(pattern));
}