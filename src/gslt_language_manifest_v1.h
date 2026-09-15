#ifndef CETTA_GSLT_LANGUAGE_MANIFEST_V1_H
#define CETTA_GSLT_LANGUAGE_MANIFEST_V1_H

#include "atom.h"

/* Existing v1 manifest view, borrowed from the parsed source Atom tree.
 * Decoding chooses a declared profile but does not load or execute semantics.
 * The caller retains the source arena and symbol table while using any
 * returned field. */
enum {
    GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES = 64,
};

typedef struct {
    const char *name;
    const char *profile_name;
    const char *profile_names[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES];
    uint32_t profile_count;
    const char *syntax_backend;
    const char *term_abi;
    const char *semantic_sources[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES];
    uint32_t semantic_source_count;
    const char *program_nil;
    const char *program_cons;
    const char *entry_relation;
    uint32_t entry_arity;
    uint32_t program_position;
    uint32_t result_position;
    bool has_entry;
    const char *query_relation;
    uint32_t query_arity;
    bool has_query_entry;
    const char *classify_relation;
    const char *produce_relation;
    const char *observe_relation;
    const char *produced_nil;
    const char *produced_cons;
    bool has_request_pipeline;
    const char *observation;
} GsltLanguageManifest;

bool cetta_gslt_language_manifest_parse_v1(
    Atom *root, const char *selected_profile, GsltLanguageManifest *manifest,
    char *error, size_t error_size);

#endif
