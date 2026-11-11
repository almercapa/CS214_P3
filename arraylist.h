#ifndef ARRAYLIST_H
#define ARRAYLIST_H

typedef struct {
    char **data;
    int    len;
    int    cap;
} arraylist_t;

void   al_init(arraylist_t *l);
void   al_push(arraylist_t *l, char *s);
char **al_take(arraylist_t *l, int *len_out);
void   al_free(arraylist_t *l);

#endif