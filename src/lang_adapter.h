#ifndef CETTA_LANG_ADAPTER_H
#define CETTA_LANG_ADAPTER_H

#include "session.h"
#include "lang.h"
#include "term_universe.h"

#define CETTA_RUNTIME_ADAPT_ERR_GENERIC (-1)
#define CETTA_RUNTIME_ADAPT_ERR_HELPER_MISSING (-2)

int cetta_language_parse_text_ids(const CettaLanguageSpec *lang, const char *text,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids);
int cetta_language_parse_file_ids(const CettaLanguageSpec *lang, const char *path,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids);
int cetta_language_parse_text_ids_for_session(const CettaEvalSession *session,
                                              const char *text,
                                              Arena *arena,
                                              TermUniverse *universe,
                                              AtomId **out_ids);
int cetta_language_parse_file_ids_for_session(const CettaEvalSession *session,
                                              const char *path,
                                              Arena *arena,
                                              TermUniverse *universe,
                                              AtomId **out_ids);
int cetta_language_adapt_runtime_atoms_for_session(
    const CettaEvalSession *session,
    Space *context_space,
    Atom *surface_atom,
    Arena *arena,
    Atom ***out_atoms);

#endif /* CETTA_LANG_ADAPTER_H */
