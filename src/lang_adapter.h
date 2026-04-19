#ifndef CETTA_LANG_ADAPTER_H
#define CETTA_LANG_ADAPTER_H

#include "lang.h"
#include "term_universe.h"

int cetta_language_parse_text_ids(const CettaLanguageSpec *lang, const char *text,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids);
int cetta_language_parse_file_ids(const CettaLanguageSpec *lang, const char *path,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids);

#endif /* CETTA_LANG_ADAPTER_H */
