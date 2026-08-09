#define _XOPEN_SOURCE 700

#include "gslt_language_runtime.h"

#include "parser.h"
#include "native_sha256.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES = 64,
};

struct CettaGsltLanguage {
    char *name;
    char *profile_name;
    char *syntax_backend;
    char *program_nil;
    char *program_cons;
    char *entry_relation;
    char *classify_relation;
    char *produce_relation;
    char *observe_relation;
    char *produced_nil;
    char *produced_cons;
    char *observation;
    uint32_t entry_arity;
    uint32_t program_position;
    uint32_t result_position;
    CettaGsltHornProgram *semantics;
    CettaGsltCompiledProgram *compiled_semantics;
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
    const char *classify_relation;
    const char *produce_relation;
    const char *observe_relation;
    const char *produced_nil;
    const char *produced_cons;
    bool has_request_pipeline;
    const char *observation;
} GsltLanguageManifest;

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
    if (atom->kind == ATOM_SYMBOL)
        return atom_name_cstr((Atom *)atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
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

static char *language_strdup(const char *text) {
    if (!text)
        return NULL;
    size_t length = strlen(text);
    char *copy = cetta_malloc(length + 1u);
    memcpy(copy, text, length + 1u);
    return copy;
}

static bool language_read_file(const char *path, char **text,
                               char *error, size_t error_size) {
    FILE *stream = fopen(path, "rb");
    if (!stream)
        return language_error(
            error, error_size, "cannot open %s: %s", path, strerror(errno));
    bool ok = false;
    char *bytes = NULL;
    if (fseek(stream, 0, SEEK_END) != 0) {
        language_error(error, error_size, "cannot seek %s", path);
        goto done;
    }
    long end = ftell(stream);
    if (end < 0 || (uint64_t)end > SIZE_MAX - 1u ||
        fseek(stream, 0, SEEK_SET) != 0) {
        language_error(error, error_size, "cannot size %s", path);
        goto done;
    }
    bytes = cetta_malloc((size_t)end + 1u);
    if ((size_t)end > 0u &&
        fread(bytes, 1u, (size_t)end, stream) != (size_t)end) {
        language_error(error, error_size, "cannot read %s", path);
        goto done;
    }
    if (memchr(bytes, 0, (size_t)end)) {
        language_error(error, error_size, "%s contains a NUL byte", path);
        goto done;
    }
    bytes[end] = '\0';
    *text = bytes;
    bytes = NULL;
    ok = true;

done:
    free(bytes);
    (void)fclose(stream);
    return ok;
}

static bool language_manifest_parse(Atom *root,
                                    const char *selected_profile,
                                    GsltLanguageManifest *manifest,
                                    char *error, size_t error_size) {
    memset(manifest, 0, sizeof(*manifest));
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
                arity == 0u || program_position >= arity ||
                result_position >= arity ||
                program_position == result_position)
                return language_error(error, error_size,
                                      "manifest has an invalid entry relation");
            manifest->entry_relation = relation;
            manifest->entry_arity = arity;
            manifest->program_position = program_position;
            manifest->result_position = result_position;
            manifest->has_entry = true;
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
                    if (manifest->semantic_source_count >=
                        GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES)
                        return language_error(
                            error, error_size,
                            "manifest has too many composed semantic sources");
                    manifest->semantic_sources[
                        manifest->semantic_source_count++] = source;
                }
            }
        } else {
            return language_error(error, error_size,
                                  "manifest contains an unknown field");
        }
    }
    if (!manifest->name || !manifest->syntax_backend ||
        !manifest->term_abi || manifest->semantic_source_count == 0u ||
        !manifest->program_nil || !manifest->program_cons ||
        manifest->has_entry == manifest->has_request_pipeline ||
        !manifest->observation)
        return language_error(error, error_size,
                              "manifest omits a required stage");
    if (selected_profile && !manifest->profile_name)
        return language_error(error, error_size,
                              "manifest does not declare the selected profile");
    if (strcmp(manifest->term_abi, "finite-horn-quote-v1") != 0)
        return language_error(error, error_size,
                              "manifest requests an unsupported term ABI");
    if (strcmp(manifest->observation, "bag") != 0)
        return language_error(error, error_size,
                              "manifest requests an unsupported observation");
    return true;
}

static bool language_optional_text_equal(const char *left,
                                         const char *right) {
    return (!left && !right) ||
        (left && right && strcmp(left, right) == 0);
}

static bool language_manifest_matches_descriptor(
    const GsltLanguageManifest *manifest,
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    char *error, size_t error_size) {
    const CettaGsltRequestPipelineV1 *pipeline =
        descriptor->request_pipeline;
    if (strcmp(manifest->name, descriptor->name) != 0 ||
        !language_optional_text_equal(
            manifest->profile_name, descriptor->profile_name) ||
        strcmp(manifest->syntax_backend, descriptor->syntax_backend) != 0 ||
        strcmp(manifest->term_abi, descriptor->term_abi) != 0 ||
        strcmp(manifest->program_nil, descriptor->program_nil) != 0 ||
        strcmp(manifest->program_cons, descriptor->program_cons) != 0 ||
        strcmp(manifest->observation, descriptor->observation) != 0 ||
        manifest->semantic_source_count !=
            descriptor->semantic_source_count ||
        manifest->has_entry != (descriptor->entry_relation != NULL) ||
        manifest->has_request_pipeline != (pipeline != NULL) ||
        !language_optional_text_equal(
            manifest->entry_relation, descriptor->entry_relation) ||
        manifest->entry_arity != descriptor->entry_arity ||
        manifest->program_position != descriptor->program_position ||
        manifest->result_position != descriptor->result_position ||
        !language_optional_text_equal(
            manifest->classify_relation,
            pipeline ? pipeline->classify_relation : NULL) ||
        !language_optional_text_equal(
            manifest->produce_relation,
            pipeline ? pipeline->produce_relation : NULL) ||
        !language_optional_text_equal(
            manifest->observe_relation,
            pipeline ? pipeline->observe_relation : NULL) ||
        !language_optional_text_equal(
            manifest->produced_nil,
            pipeline ? pipeline->produced_nil : NULL) ||
        !language_optional_text_equal(
            manifest->produced_cons,
            pipeline ? pipeline->produced_cons : NULL))
        return language_error(
            error, error_size,
            "embedded GSLT descriptor differs from its authored manifest");
    for (uint32_t index = 0u;
         index < manifest->semantic_source_count; index++) {
        const char *source =
            descriptor->semantic_sources[index].input.source;
        if (!source || strcmp(
                manifest->semantic_sources[index], source) != 0)
            return language_error(
                error, error_size,
                "embedded GSLT source order differs from its authored manifest");
    }
    return true;
}

static bool language_validate_embedded_manifest(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    char *error, size_t error_size) {
    const CettaGsltEmbeddedSourceV1 *embedded = &descriptor->manifest;
    if (!embedded->input.bytes || embedded->input.length == 0u ||
        !embedded->input.source || !embedded->sha256 ||
        memchr(embedded->input.bytes, 0, embedded->input.length))
        return language_error(error, error_size,
                              "embedded GSLT manifest is incomplete");
    char digest[65];
    cetta_native_sha256_hex(
        embedded->input.bytes, embedded->input.length, digest);
    if (strcmp(digest, embedded->sha256) != 0 ||
        strcmp(digest, descriptor->manifest_sha256) != 0)
        return language_error(error, error_size,
                              "embedded GSLT manifest digest changed");
    char *text = cetta_malloc(embedded->input.length + 1u);
    memcpy(text, embedded->input.bytes, embedded->input.length);
    text[embedded->input.length] = '\0';
    Arena arena;
    arena_init(&arena);
    Atom **forms = NULL;
    int form_count = parse_metta_text(text, &arena, &forms);
    GsltLanguageManifest manifest;
    bool valid = form_count == 1 && forms &&
        language_manifest_parse(
            forms[0], descriptor->profile_name,
            &manifest, error, error_size) &&
        language_manifest_matches_descriptor(
            &manifest, descriptor, error, error_size);
    if (!valid && (!error || error_size == 0u || error[0] == '\0'))
        language_error(error, error_size,
                       "cannot parse embedded GSLT manifest");
    free(forms);
    arena_free(&arena);
    free(text);
    return valid;
}

static bool language_resolve_relative(
    const char *manifest_path, const char *relative,
    char output[PATH_MAX], char *error, size_t error_size) {
    if (!manifest_path || !relative || relative[0] == '/')
        return language_error(error, error_size,
                              "semantic sources must be relative paths");
    const char *slash = strrchr(manifest_path, '/');
    size_t directory_length = slash
        ? (size_t)(slash - manifest_path) : 1u;
    int written = slash
        ? snprintf(output, PATH_MAX, "%.*s/%s",
                   (int)directory_length, manifest_path, relative)
        : snprintf(output, PATH_MAX, "./%s", relative);
    if (written < 0 || written >= PATH_MAX)
        return language_error(error, error_size,
                              "semantic source path is too long");
    char *resolved = realpath(output, NULL);
    if (!resolved)
        return language_error(
            error, error_size, "cannot resolve %s: %s",
            output, strerror(errno));
    if (strlen(resolved) >= PATH_MAX) {
        free(resolved);
        return language_error(error, error_size,
                              "semantic source path is too long");
    }
    strcpy(output, resolved);
    free(resolved);
    return true;
}

bool cetta_gslt_language_load_manifest(
    const char *manifest_path, CettaGsltLanguage **out,
    char *error, size_t error_size) {
    if (!manifest_path || !out)
        return language_error(error, error_size,
                              "invalid GSLT language load request");
    *out = NULL;
    char *resolved_manifest = realpath(manifest_path, NULL);
    char *manifest_text = NULL;
    Atom **forms = NULL;
    Arena arena;
    bool arena_ready = false;
    CettaGsltLanguage *language = NULL;
    char *semantic_paths[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES] = {0};
    const char *semantic_path_views[GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES] = {0};
    GsltLanguageManifest manifest;
    bool ok = false;
    if (!resolved_manifest) {
        language_error(error, error_size, "cannot resolve %s: %s",
                       manifest_path, strerror(errno));
        goto done;
    }
    if (!language_read_file(
            resolved_manifest, &manifest_text, error, error_size))
        goto done;
    arena_init(&arena);
    arena_ready = true;
    int form_count = parse_metta_text(manifest_text, &arena, &forms);
    if (form_count != 1 || !forms ||
        !language_manifest_parse(
            forms[0], NULL, &manifest, error, error_size))
        goto done;
    for (uint32_t index = 0u; index < manifest.semantic_source_count;
         index++) {
        char path[PATH_MAX];
        if (!language_resolve_relative(
                resolved_manifest, manifest.semantic_sources[index],
                path, error, error_size))
            goto done;
        semantic_paths[index] = language_strdup(path);
        semantic_path_views[index] = semantic_paths[index];
    }
    language = cetta_malloc(sizeof(*language));
    memset(language, 0, sizeof(*language));
    language->name = language_strdup(manifest.name);
    language->profile_name = language_strdup(manifest.profile_name);
    language->syntax_backend = language_strdup(manifest.syntax_backend);
    language->program_nil = language_strdup(manifest.program_nil);
    language->program_cons = language_strdup(manifest.program_cons);
    language->entry_relation = language_strdup(manifest.entry_relation);
    language->classify_relation = language_strdup(manifest.classify_relation);
    language->produce_relation = language_strdup(manifest.produce_relation);
    language->observe_relation = language_strdup(manifest.observe_relation);
    language->produced_nil = language_strdup(manifest.produced_nil);
    language->produced_cons = language_strdup(manifest.produced_cons);
    language->observation = language_strdup(manifest.observation);
    language->entry_arity = manifest.entry_arity;
    language->program_position = manifest.program_position;
    language->result_position = manifest.result_position;
    if (!cetta_gslt_horn_program_load_paths(
            semantic_path_views, manifest.semantic_source_count,
            &language->semantics, error, error_size))
        goto done;
    *out = language;
    language = NULL;
    ok = true;

done:
    for (uint32_t index = 0u;
         index < GSLT_LANGUAGE_MAX_SEMANTIC_SOURCES; index++)
        free(semantic_paths[index]);
    cetta_gslt_language_free(language);
    free(forms);
    if (arena_ready)
        arena_free(&arena);
    free(manifest_text);
    free(resolved_manifest);
    return ok;
}

bool cetta_gslt_language_load_embedded(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    CettaGsltLanguage **out, char *error, size_t error_size) {
    return cetta_gslt_language_load_embedded_for_realization(
        descriptor, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        out, error, error_size);
}

bool cetta_gslt_language_load_embedded_for_realization(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    CettaGsltRealization realization,
    CettaGsltLanguage **out, char *error, size_t error_size) {
    bool has_entry = descriptor && descriptor->entry_relation &&
        descriptor->entry_arity > 0u;
    bool has_pipeline = descriptor && descriptor->request_pipeline &&
        descriptor->request_pipeline->classify_relation &&
        descriptor->request_pipeline->produce_relation &&
        descriptor->request_pipeline->observe_relation &&
        descriptor->request_pipeline->produced_nil &&
        descriptor->request_pipeline->produced_cons;
    if (!descriptor || !out ||
        (realization != CETTA_GSLT_REALIZATION_HORN_REFERENCE &&
         realization != CETTA_GSLT_REALIZATION_COMPILED_WORKLIST) ||
        !descriptor->name ||
        !descriptor->syntax_backend || !descriptor->term_abi ||
        !descriptor->manifest.input.bytes ||
        descriptor->manifest.input.length == 0u ||
        !descriptor->manifest.input.source ||
        !descriptor->manifest.sha256 ||
        !descriptor->semantic_sources ||
        descriptor->semantic_source_count == 0u ||
        !descriptor->compiled_plan.bytes ||
        descriptor->compiled_plan.length == 0u ||
        !descriptor->compiled_plan.sha256 ||
        !descriptor->program_nil || !descriptor->program_cons ||
        has_entry == has_pipeline ||
        (has_entry &&
         (descriptor->entry_arity == UINT32_MAX ||
          descriptor->program_position >= descriptor->entry_arity ||
          descriptor->result_position >= descriptor->entry_arity ||
          descriptor->program_position == descriptor->result_position)) ||
        !descriptor->observation || !descriptor->manifest_sha256 ||
        !descriptor->compiler_sha256)
        return language_error(error, error_size,
                              "invalid embedded GSLT language descriptor");
    *out = NULL;
    if (!language_validate_embedded_manifest(
            descriptor, error, error_size))
        return false;
    if (strcmp(descriptor->term_abi, "finite-horn-quote-v1") != 0 ||
        strcmp(descriptor->observation, "bag") != 0)
        return language_error(error, error_size,
                              "unsupported embedded GSLT language contract");
    CettaGsltHornInput *inputs = cetta_malloc(
        sizeof(*inputs) * descriptor->semantic_source_count);
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++) {
        const CettaGsltEmbeddedSourceV1 *source =
            &descriptor->semantic_sources[index];
        char digest[65];
        if (!source->input.bytes || source->input.length == 0u ||
            !source->input.source || !source->sha256) {
            free(inputs);
            return language_error(error, error_size,
                                  "embedded semantic source is incomplete");
        }
        cetta_native_sha256_hex(
            source->input.bytes, source->input.length, digest);
        if (strcmp(digest, source->sha256) != 0) {
            free(inputs);
            return language_error(error, error_size,
                                  "embedded semantic source digest changed");
        }
        inputs[index] = source->input;
    }
    CettaGsltLanguage *language = cetta_malloc(sizeof(*language));
    memset(language, 0, sizeof(*language));
    language->name = language_strdup(descriptor->name);
    language->profile_name = language_strdup(descriptor->profile_name);
    language->syntax_backend = language_strdup(descriptor->syntax_backend);
    language->program_nil = language_strdup(descriptor->program_nil);
    language->program_cons = language_strdup(descriptor->program_cons);
    language->entry_relation = language_strdup(descriptor->entry_relation);
    if (has_pipeline) {
        language->classify_relation = language_strdup(
            descriptor->request_pipeline->classify_relation);
        language->produce_relation = language_strdup(
            descriptor->request_pipeline->produce_relation);
        language->observe_relation = language_strdup(
            descriptor->request_pipeline->observe_relation);
        language->produced_nil = language_strdup(
            descriptor->request_pipeline->produced_nil);
        language->produced_cons = language_strdup(
            descriptor->request_pipeline->produced_cons);
    }
    language->observation = language_strdup(descriptor->observation);
    language->entry_arity = descriptor->entry_arity;
    language->program_position = descriptor->program_position;
    language->result_position = descriptor->result_position;
    CettaGsltHornProgram *admitted_source = NULL;
    bool loaded = cetta_gslt_horn_program_load_inputs(
        inputs, descriptor->semantic_source_count,
        &admitted_source, error, error_size);
    free(inputs);
    if (loaded)
        loaded = cetta_gslt_compiled_program_load_v1(
            &descriptor->compiled_plan, &language->compiled_semantics,
            error, error_size);
    if (loaded)
        loaded = cetta_gslt_compiled_program_matches_source_v1(
            language->compiled_semantics, admitted_source,
            error, error_size);
    if (!loaded) {
        cetta_gslt_horn_program_free(admitted_source);
        cetta_gslt_language_free(language);
        return false;
    }
    if (realization == CETTA_GSLT_REALIZATION_HORN_REFERENCE)
        language->semantics = admitted_source;
    else
        cetta_gslt_horn_program_free(admitted_source);
    *out = language;
    return true;
}

void cetta_gslt_language_free(CettaGsltLanguage *language) {
    if (!language)
        return;
    cetta_gslt_compiled_program_free(language->compiled_semantics);
    cetta_gslt_horn_program_free(language->semantics);
    free(language->name);
    free(language->profile_name);
    free(language->syntax_backend);
    free(language->program_nil);
    free(language->program_cons);
    free(language->entry_relation);
    free(language->classify_relation);
    free(language->produce_relation);
    free(language->observe_relation);
    free(language->produced_nil);
    free(language->produced_cons);
    free(language->observation);
    free(language);
}

const char *cetta_gslt_language_name(const CettaGsltLanguage *language) {
    return language ? language->name : NULL;
}

const char *cetta_gslt_language_syntax_backend(
    const CettaGsltLanguage *language) {
    return language ? language->syntax_backend : NULL;
}

const char *cetta_gslt_language_observation(
    const CettaGsltLanguage *language) {
    return language ? language->observation : NULL;
}

size_t cetta_gslt_language_semantic_rule_count(
    const CettaGsltLanguage *language) {
    if (!language)
        return 0u;
    return language->compiled_semantics
        ? cetta_gslt_compiled_program_rule_count(language->compiled_semantics)
        : cetta_gslt_horn_program_rule_count(language->semantics);
}

static Atom *language_program_selected(
    const CettaGsltLanguage *language,
    Atom *const *forms, const size_t *occurrences,
    size_t form_count, Arena *arena) {
    if (form_count > (size_t)INT64_MAX)
        return NULL;
    Atom *program = atom_symbol(arena, language->program_nil);
    for (size_t index = form_count; index > 0u; index--) {
        size_t occurrence_index = occurrences
            ? occurrences[index - 1u] : index - 1u;
        if (occurrence_index > (size_t)INT64_MAX)
            return NULL;
        Atom *quoted = cetta_gslt_quote_atom_v1(arena, forms[index - 1u]);
        Atom *occurrence = atom_expr2(
            arena, atom_symbol(arena, "gslt-source-occurrence"),
            atom_int(arena, (int64_t)occurrence_index));
        if (!quoted || !occurrence)
            return NULL;
        Atom *elements[] = {
            atom_symbol(arena, language->program_cons),
            occurrence,
            quoted,
            program,
        };
        program = atom_expr(arena, elements, 4u);
    }
    return program;
}

static Atom *language_program(
    const CettaGsltLanguage *language,
    Atom *const *forms, size_t form_count, Arena *arena) {
    return language_program_selected(
        language, forms, NULL, form_count, arena);
}

static bool language_run_query(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Arena *output_arena, Atom *query,
    CettaGsltHornLimits limits, CettaGsltHornResult *result,
    char *error, size_t error_size) {
    if (realization == CETTA_GSLT_REALIZATION_HORN_REFERENCE)
        return language->semantics
            ? cetta_gslt_horn_query(
                language->semantics, output_arena, query, limits,
                result, error, error_size)
            : language_error(error, error_size,
                             "reference realization was not loaded");
    if (realization == CETTA_GSLT_REALIZATION_COMPILED_WORKLIST)
        return language->compiled_semantics
            ? cetta_gslt_compiled_query_v1(
                language->compiled_semantics, output_arena, query, limits,
                result, error, error_size)
            : language_error(error, error_size,
                             "compiled realization was not loaded");
    return language_error(error, error_size, "unknown GSLT realization");
}

static uint64_t language_u64_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static void language_accumulate_stage(
    CettaGsltLanguageResult *result, const CettaGsltHornResult *stage) {
    result->rule_attempts = language_u64_add_sat(
        result->rule_attempts, stage->rule_attempts);
    result->rule_matches = language_u64_add_sat(
        result->rule_matches, stage->rule_matches);
    if (stage->max_depth_observed > result->max_depth_observed)
        result->max_depth_observed = stage->max_depth_observed;
    result->outcome = stage->outcome;
}

static bool language_stage_limits(
    const CettaGsltLanguageResult *result,
    CettaGsltHornLimits requested, CettaGsltHornLimits *stage) {
    if (result->rule_attempts >= requested.max_rule_attempts)
        return false;
    *stage = requested;
    stage->max_rule_attempts -= result->rule_attempts;
    return true;
}

static bool language_relation_answer(
    const Atom *answer, const char *relation, CettaExprLen arity) {
    return answer && answer->kind == ATOM_EXPR &&
        answer->expr.len == arity + 1u &&
        answer->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(answer->expr.elems[0]), relation) == 0;
}

static void language_clear_answers(CettaGsltLanguageResult *result) {
    free(result->answers);
    free(result->evidence);
    result->answers = NULL;
    result->evidence = NULL;
    result->answer_count = 0u;
}

static bool language_append_answer(
    CettaGsltLanguageResult *result, Atom *answer, Atom *evidence,
    CettaGsltHornLimits limits, char *error, size_t error_size) {
    if (result->answer_count >= limits.max_answers) {
        language_clear_answers(result);
        result->outcome = CETTA_GSLT_HORN_ANSWER_LIMIT;
        return true;
    }
    if (result->answer_count == SIZE_MAX / sizeof(*result->answers))
        return language_error(error, error_size,
                              "GSLT language answer bag is too large");
    result->answers = cetta_realloc(
        result->answers,
        sizeof(*result->answers) * (result->answer_count + 1u));
    result->evidence = cetta_realloc(
        result->evidence,
        sizeof(*result->evidence) * (result->answer_count + 1u));
    result->answers[result->answer_count] = answer;
    result->evidence[result->answer_count] = evidence;
    result->answer_count++;
    return true;
}

static Atom *language_pipeline_query(
    Arena *arena, const char *relation,
    Atom *first, Atom *second, Atom *third, Atom *fourth) {
    Atom *elements[] = {
        atom_symbol(arena, relation), first, second, third, fourth,
    };
    return atom_expr(arena, elements, 5u);
}

static bool language_execute_request_pipeline(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size) {
    Atom **requests = NULL;
    size_t request_count = 0u;
    Atom **contents = NULL;
    size_t *content_occurrences = NULL;
    size_t content_count = 0u;
    bool ok = false;
    if (form_count > (size_t)INT64_MAX)
        return language_error(error, error_size,
                              "source document has too many forms");

    for (size_t index = 0u; index < form_count; index++) {
        Atom *occurrence = atom_expr2(
            output_arena,
            atom_symbol(output_arena, "gslt-source-occurrence"),
            atom_int(output_arena, (int64_t)index));
        Atom *quoted = cetta_gslt_quote_atom_v1(output_arena, forms[index]);
        Atom *request = atom_var_with_id(
            output_arena, "gslt-request", fresh_var_id());
        Atom *query_elements[] = {
            atom_symbol(output_arena, language->classify_relation),
            occurrence, quoted, request,
        };
        Atom *query = atom_expr(output_arena, query_elements, 4u);
        CettaGsltHornLimits stage_limits;
        if (!occurrence || !quoted || !query ||
            !language_stage_limits(result, limits, &stage_limits)) {
            result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
            goto completed;
        }
        CettaGsltHornResult stage = {0};
        if (!language_run_query(
                language, realization, output_arena, query, stage_limits,
                &stage, error, error_size)) {
            cetta_gslt_horn_result_free(&stage);
            goto done;
        }
        language_accumulate_stage(result, &stage);
        if (stage.outcome != CETTA_GSLT_HORN_COMPLETED) {
            cetta_gslt_horn_result_free(&stage);
            goto completed;
        }
        if (stage.answer_count == 0u) {
            contents = cetta_realloc(
                contents, sizeof(*contents) * (content_count + 1u));
            content_occurrences = cetta_realloc(
                content_occurrences,
                sizeof(*content_occurrences) * (content_count + 1u));
            contents[content_count] = forms[index];
            content_occurrences[content_count++] = index;
        } else {
            for (size_t answer_index = 0u;
                 answer_index < stage.answer_count; answer_index++) {
                Atom *answer = stage.answers[answer_index];
                if (!language_relation_answer(
                        answer, language->classify_relation, 3u)) {
                    cetta_gslt_horn_result_free(&stage);
                    language_error(
                        error, error_size,
                        "request classifier returned a malformed answer");
                    goto done;
                }
                requests = cetta_realloc(
                    requests, sizeof(*requests) * (request_count + 1u));
                requests[request_count++] = answer->expr.elems[3];
            }
        }
        cetta_gslt_horn_result_free(&stage);
    }

    {
        Atom *program = language_program_selected(
            language, contents, content_occurrences,
            content_count, output_arena);
        if (!program) {
            language_error(error, error_size,
                           "cannot quote the reflective source space");
            goto done;
        }
        for (size_t request_index = 0u;
             request_index < request_count; request_index++) {
            Atom *evidence = atom_var_with_id(
                output_arena, "gslt-evidence", fresh_var_id());
            Atom *quoted_result = atom_var_with_id(
                output_arena, "gslt-result", fresh_var_id());
            Atom *produce_query = language_pipeline_query(
                output_arena, language->produce_relation,
                program, requests[request_index], evidence, quoted_result);
            CettaGsltHornLimits stage_limits;
            if (!produce_query ||
                !language_stage_limits(result, limits, &stage_limits)) {
                result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
                goto completed;
            }
            CettaGsltHornResult produced = {0};
            if (!language_run_query(
                    language, realization, output_arena,
                    produce_query, stage_limits, &produced,
                    error, error_size)) {
                cetta_gslt_horn_result_free(&produced);
                goto done;
            }
            language_accumulate_stage(result, &produced);
            if (produced.outcome != CETTA_GSLT_HORN_COMPLETED) {
                cetta_gslt_horn_result_free(&produced);
                goto completed;
            }

            Atom *bag = atom_symbol(output_arena, language->produced_nil);
            for (size_t answer_index = produced.answer_count;
                 answer_index > 0u; answer_index--) {
                Atom *answer = produced.answers[answer_index - 1u];
                if (!language_relation_answer(
                        answer, language->produce_relation, 4u)) {
                    cetta_gslt_horn_result_free(&produced);
                    language_error(
                        error, error_size,
                        "request producer returned a malformed answer");
                    goto done;
                }
                Atom *item_elements[] = {
                    atom_symbol(output_arena, "gslt-produced"),
                    answer->expr.elems[3], answer->expr.elems[4],
                };
                Atom *item = atom_expr(output_arena, item_elements, 3u);
                Atom *cons_elements[] = {
                    atom_symbol(output_arena, language->produced_cons),
                    item, bag,
                };
                bag = atom_expr(output_arena, cons_elements, 3u);
            }
            cetta_gslt_horn_result_free(&produced);

            evidence = atom_var_with_id(
                output_arena, "gslt-observation-evidence", fresh_var_id());
            quoted_result = atom_var_with_id(
                output_arena, "gslt-observed-result", fresh_var_id());
            Atom *observe_query = language_pipeline_query(
                output_arena, language->observe_relation,
                requests[request_index], bag, evidence, quoted_result);
            if (!observe_query ||
                !language_stage_limits(result, limits, &stage_limits)) {
                result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
                goto completed;
            }
            CettaGsltHornResult observed = {0};
            if (!language_run_query(
                    language, realization, output_arena,
                    observe_query, stage_limits, &observed,
                    error, error_size)) {
                cetta_gslt_horn_result_free(&observed);
                goto done;
            }
            language_accumulate_stage(result, &observed);
            if (observed.outcome != CETTA_GSLT_HORN_COMPLETED) {
                cetta_gslt_horn_result_free(&observed);
                goto completed;
            }
            for (size_t answer_index = 0u;
                 answer_index < observed.answer_count; answer_index++) {
                Atom *answer = observed.answers[answer_index];
                if (!language_relation_answer(
                        answer, language->observe_relation, 4u)) {
                    cetta_gslt_horn_result_free(&observed);
                    language_error(
                        error, error_size,
                        "request observer returned a malformed answer");
                    goto done;
                }
                Atom *value = cetta_gslt_unquote_atom_v1(
                    output_arena, answer->expr.elems[4]);
                if (!value) {
                    cetta_gslt_horn_result_free(&observed);
                    language_error(
                        error, error_size,
                        "request observer returned an invalid quoted result");
                    goto done;
                }
                if (!language_append_answer(
                        result, value, answer->expr.elems[3],
                        limits, error, error_size)) {
                    cetta_gslt_horn_result_free(&observed);
                    goto done;
                }
                if (result->outcome == CETTA_GSLT_HORN_ANSWER_LIMIT) {
                    cetta_gslt_horn_result_free(&observed);
                    goto completed;
                }
            }
            cetta_gslt_horn_result_free(&observed);
        }
    }
    result->outcome = CETTA_GSLT_HORN_COMPLETED;

completed:
    if (result->outcome != CETTA_GSLT_HORN_COMPLETED)
        language_clear_answers(result);
    ok = true;
done:
    free(content_occurrences);
    free(contents);
    free(requests);
    return ok;
}

bool cetta_gslt_language_execute_atoms(
    const CettaGsltLanguage *language,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size) {
    return cetta_gslt_language_execute_atoms_with_realization(
        language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        forms, form_count, output_arena, limits,
        result, error, error_size);
}

const char *cetta_gslt_realization_name(CettaGsltRealization realization) {
    switch (realization) {
    case CETTA_GSLT_REALIZATION_HORN_REFERENCE:
        return "horn-reference";
    case CETTA_GSLT_REALIZATION_COMPILED_WORKLIST:
        return "compiled-worklist";
    }
    return NULL;
}

bool cetta_gslt_realization_parse(
    const char *name, CettaGsltRealization *realization) {
    if (!name || !realization)
        return false;
    for (uint32_t value = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
         value <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; value++) {
        const char *candidate = cetta_gslt_realization_name(
            (CettaGsltRealization)value);
        if (candidate && strcmp(name, candidate) == 0) {
            *realization = (CettaGsltRealization)value;
            return true;
        }
    }
    return false;
}

bool cetta_gslt_language_execute_atoms_with_realization(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size) {
    if (!language || (!forms && form_count > 0u) || !output_arena || !result)
        return language_error(error, error_size,
                              "invalid GSLT language execution request");
    if (limits.max_rule_attempts == 0u || limits.max_answers == 0u ||
        limits.max_depth == 0u)
        return language_error(error, error_size,
                              "GSLT language limits must be nonzero");
    memset(result, 0, sizeof(*result));
    if (language->classify_relation)
        return language_execute_request_pipeline(
            language, realization, forms, form_count,
            output_arena, limits, result, error, error_size);
    Atom *program = language_program(
        language, forms, form_count, output_arena);
    if (!program)
        return language_error(error, error_size,
                              "cannot quote the source document");
    Atom **query_elements = arena_alloc(
        output_arena,
        sizeof(*query_elements) * (size_t)(language->entry_arity + 1u));
    query_elements[0] = atom_symbol(output_arena, language->entry_relation);
    for (uint32_t index = 0u; index < language->entry_arity; index++)
        query_elements[index + 1u] = index == language->program_position
            ? program
            : atom_var_with_id(
                  output_arena, "gslt-entry", fresh_var_id());
    Atom *query = atom_expr(
        output_arena, query_elements,
        (CettaExprLen)(language->entry_arity + 1u));
    CettaGsltHornResult horn = {0};
    bool queried = language_run_query(
        language, realization, output_arena, query, limits,
        &horn, error, error_size);
    if (!queried) {
        cetta_gslt_horn_result_free(&horn);
        return false;
    }
    result->outcome = horn.outcome;
    result->rule_attempts = horn.rule_attempts;
    result->rule_matches = horn.rule_matches;
    result->max_depth_observed = horn.max_depth_observed;
    if (horn.outcome != CETTA_GSLT_HORN_COMPLETED) {
        cetta_gslt_horn_result_free(&horn);
        return true;
    }
    result->answers = horn.answer_count
        ? cetta_malloc(sizeof(*result->answers) * horn.answer_count)
        : NULL;
    for (size_t index = 0u; index < horn.answer_count; index++) {
        Atom *answer = horn.answers[index];
        if (!answer || answer->kind != ATOM_EXPR ||
            answer->expr.len != (CettaExprLen)(language->entry_arity + 1u) ||
            answer->expr.elems[0]->kind != ATOM_SYMBOL ||
            strcmp(atom_name_cstr(answer->expr.elems[0]),
                   language->entry_relation) != 0) {
            cetta_gslt_horn_result_free(&horn);
            cetta_gslt_language_result_free(result);
            return language_error(error, error_size,
                                  "entry relation returned a malformed answer");
        }
        Atom *quoted = answer->expr.elems[language->result_position + 1u];
        Atom *value = cetta_gslt_unquote_atom_v1(output_arena, quoted);
        if (!value) {
            cetta_gslt_horn_result_free(&horn);
            cetta_gslt_language_result_free(result);
            return language_error(error, error_size,
                                  "entry relation returned an invalid quoted result");
        }
        result->answers[result->answer_count++] = value;
    }
    cetta_gslt_horn_result_free(&horn);
    return true;
}

void cetta_gslt_language_result_free(CettaGsltLanguageResult *result) {
    if (!result)
        return;
    free(result->answers);
    free(result->evidence);
    memset(result, 0, sizeof(*result));
}
