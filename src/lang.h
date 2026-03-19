#ifndef CETTA_LANG_H
#define CETTA_LANG_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    const char *name;
    const char *canonical;
    bool implemented;
    const char *note;
} CettaLanguageSpec;

const CettaLanguageSpec *cetta_language_lookup(const char *name);
void cetta_language_print_inventory(FILE *out);

#endif /* CETTA_LANG_H */
