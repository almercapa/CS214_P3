#include <stdio.h>
#include <stdlib.h>

#include "arraylist.h"

void al_init(arraylist_t *l)
{
    l->data = NULL;
    l->len = 0;
    l->cap = 0;
}

void al_push(arraylist_t *l, char *s)
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

char **al_take(arraylist_t *l, int *len_out)
{
    int n = l->len;
    al_push(l, NULL);
    char **out = l->data;
    if (len_out)
        *len_out = n;
    al_init(l);
    return out;
}

void al_free(arraylist_t *l)
{
    for (int i = 0; i < l->len; i++)
        free(l->data[i]);
    free(l->data);
    al_init(l);
}