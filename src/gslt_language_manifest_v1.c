#include "gslt_language_manifest_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool language_error(char *buffer, size_t size,
                           const char *format, ...) {
    if (buffer && size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool language_expr(const Atom *atom, const char *head,
                          CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(atom->expr.elems[0]), head) == 0;
}

static bool language_has_head(const Atom *atom, const char *head) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(atom->expr.elems[0]), head) == 0;
}

static const char *language_text(const Atom *atom) {
    if (!atom)
        return NULL;
    const char *text = NULL;
    if (atom->kind == ATOM_SYMBOL)
        text = atom_name_cstr((Atom *)atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        text = atom->ground.sval;
    if (text && *text)
        return text;
    return NULL;
}

static bool language_u32(const Atom *atom, uint32_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > UINT32_MAX)
        return false;
    *value = (uint32_t)atom->ground.ival;
    return true;
}

bool cetta_gslt_language_manifest_parse_v1(Atom *root,
                                    const char *selected_profile,
                                    GsltLanguageManifest *manifest,
                                    char *error, size_t error_size) {
    if (!manifest)
        return language_error(error, error_size, "manifest output is absent");
    memset(manifest, 0, sizeof(*manifest));
    const char *extension_sources[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES];
    uint32_t extension_count = 0u;
    if (!root || root->kind != ATOM_EXPR || root->expr.len < 2u ||
        root->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(root->expr.elems[0]),
               "gslt-language-v1") != 0)
        return language_error(
            error, error_size, "manifest root must be gslt-language-v1");
    for (CettaExprIndex index = 1u; index < root->expr.len; index++) {
        Atom *field = root->expr.elems[index];
        const char *text = NULL;
        if (language_expr(field, "name", 2u)) {
            text = language_text(field->expr.elems[1]);
            if (manifest->name || !text)
                return language_error(error, error_size,
                                      "manifest has an invalid name");
            manifest->name = text;
        } else if (language_expr(field, "syntax-backend", 2u)) {
            text = language_text(field->expr.elems[1]);
            if (manifest->syntax_backend || !text)
                return language_error(
                    error, error_size,
                    "manifest has an invalid syntax backend");
            manifest->syntax_backend = text;
        } else if (language_expr(field, "term-abi", 2u)) {
            text = language_text(field->expr.elems[1]);
            if (manifest->term_abi || !text)
                return language_error(error, error_size,
                                      "manifest has an invalid term ABI");
            manifest->term_abi = text;
        } else if (language_expr(field, "semantic-source", 2u)) {
            text = language_text(field->expr.elems[1]);
            if (!text || manifest->semantic_source_count >=
                             GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES)
                return language_error(error, error_size,
                                      "manifest has an invalid semantic source");
            manifest->semantic_sources[manifest->semantic_source_count++] = text;
        } else if (language_expr(field, "program-carrier", 3u)) {
            const char *nil = language_text(field->expr.elems[1]);
            const char *cons = language_text(field->expr.elems[2]);
            if (manifest->program_nil || manifest->program_cons ||
                !nil || !cons)
                return language_error(error, error_size,
                                      "manifest has an invalid program carrier");
            manifest->program_nil = nil;
            manifest->program_cons = cons;
        } else if (language_expr(field, "entry", 5u)) {
            const char *relation = language_text(field->expr.elems[1]);
            uint32_t arity, program_position, result_position;
            if (manifest->has_entry || !relation ||
                !language_u32(field->expr.elems[2], &arity) ||
                !language_u32(field->expr.elems[3], &program_position) ||
                !language_u32(field->expr.elems[4], &result_position) ||
                arity == 0u || arity == UINT32_MAX || program_position >= arity ||
                result_position >= arity ||
                program_position == result_position)
                return language_error(error, error_size,
                                      "manifest has an invalid entry relation");
            manifest->entry_relation = relation;
            manifest->entry_arity = arity;
            manifest->program_position = program_position;
            manifest->result_position = result_position;
            manifest->has_entry = true;
        } else if (language_expr(field, "query-entry", 3u)) {
            const char *relation = language_text(field->expr.elems[1]);
            uint32_t arity;
            if (manifest->has_query_entry || !relation ||
                !language_u32(field->expr.elems[2], &arity) ||
                arity == 0u || arity == UINT32_MAX)
                return language_error(
                    error, error_size,
                    "manifest has an invalid query entry");
            manifest->query_relation = relation;
            manifest->query_arity = arity;
            manifest->has_query_entry = true;
        } else if (language_expr(field, "request-pipeline", 6u)) {
            const char *classify = language_text(field->expr.elems[1]);
            const char *produce = language_text(field->expr.elems[2]);
            const char *observe = language_text(field->expr.elems[3]);
            const char *nil = language_text(field->expr.elems[4]);
            const char *cons = language_text(field->expr.elems[5]);
            if (manifest->has_request_pipeline || !classify || !produce ||
                !observe || !nil || !cons)
                return language_error(
                    error, error_size,
                    "manifest has an invalid request pipeline");
            manifest->classify_relation = classify;
            manifest->produce_relation = produce;
            manifest->observe_relation = observe;
            manifest->produced_nil = nil;
            manifest->produced_cons = cons;
            manifest->has_request_pipeline = true;
        } else if (language_expr(field, "observation", 2u)) {
            text = language_text(field->expr.elems[1]);
            if (manifest->observation || !text)
                return language_error(error, error_size,
                                      "manifest has an invalid observation");
            manifest->observation = text;
        } else if (language_has_head(field, "profile")) {
            if (field->expr.len < 3u)
                return language_error(error, error_size,
                                      "manifest has a malformed profile");
            const char *profile = language_text(field->expr.elems[1]);
            if (!profile || manifest->profile_count >=
                                GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES)
                return language_error(error, error_size,
                                      "manifest has an invalid profile");
            for (uint32_t prior = 0u; prior < manifest->profile_count;
                 prior++) {
                if (strcmp(manifest->profile_names[prior], profile) == 0)
                    return language_error(error, error_size,
                                          "manifest has a duplicate profile");
            }
            manifest->profile_names[manifest->profile_count++] = profile;
            bool selected = selected_profile &&
                strcmp(selected_profile, profile) == 0;
            if (selected)
                manifest->profile_name = profile;
            uint32_t profile_source_count = 0u;
            for (CettaExprIndex profile_index = 2u;
                 profile_index < field->expr.len; profile_index++) {
                Atom *extension = field->expr.elems[profile_index];
                if (!language_expr(extension, "semantic-source", 2u))
                    return language_error(
                        error, error_size,
                        "manifest profiles support only semantic sources");
                const char *source = language_text(extension->expr.elems[1]);
                if (!source || profile_source_count == UINT32_MAX)
                    return language_error(error, error_size,
                                          "manifest profile has an invalid source");
                profile_source_count++;
                if (selected) {
                    if (extension_count >=
                        GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES)
                        return language_error(
                            error, error_size,
                            "manifest has too many composed semantic sources");
                    extension_sources[extension_count++] = source;
                }
            }
        } else {
            return language_error(error, error_size,
                                  "manifest contains an unknown field");
        }
    }
    unsigned entry_modes = (manifest->has_entry ? 1u : 0u) +
        (manifest->has_query_entry ? 1u : 0u) +
        (manifest->has_request_pipeline ? 1u : 0u);
    bool carrier_is_complete =
        manifest->program_nil && manifest->program_cons;
    if (!manifest->name || !manifest->syntax_backend ||
        !manifest->term_abi || manifest->semantic_source_count == 0u ||
        entry_modes != 1u ||
        (manifest->has_query_entry
             ? carrier_is_complete
             : !carrier_is_complete) ||
        !manifest->observation)
        return language_error(error, error_size,
                              "manifest omits a required stage");
    if (selected_profile && !manifest->profile_name)
        return language_error(error, error_size,
                              "manifest does not declare the selected profile");
    /* Profiles extend the complete ordered base, regardless of where the
     * profile field occurs among the manifest's fields. */
    if (extension_count > GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES -
                              manifest->semantic_source_count)
        return language_error(error, error_size,
                              "manifest has too many composed semantic sources");
    for (uint32_t i = 0; i < extension_count; i++)
        manifest->semantic_sources[manifest->semantic_source_count++] =
            extension_sources[i];
    if (strcmp(manifest->term_abi, "finite-horn-quote-v1") != 0)
        return language_error(error, error_size,
                              "manifest requests an unsupported term ABI");
    if (strcmp(manifest->observation, "bag") != 0)
        return language_error(error, error_size,
                              "manifest requests an unsupported observation");
    return true;
}
