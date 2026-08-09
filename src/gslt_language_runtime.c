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
    char *syntax_backend;
    char *program_nil;
    char *program_cons;
    char *entry_relation;
    char *observation;
    uint32_t entry_arity;
    uint32_t program_position;
    uint32_t result_position;
    CettaGsltHornProgram *semantics;
};

typedef struct {
    const char *name;
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
        } else if (language_expr(field, "observation", 2u)) {
            text = language_text(field->expr.elems[1]);
            if (manifest->observation || !text)
                return language_error(error, error_size,
                                      "manifest has an invalid observation");
            manifest->observation = text;
        } else {
            return language_error(error, error_size,
                                  "manifest contains an unknown field");
        }
    }
    if (!manifest->name || !manifest->syntax_backend ||
        !manifest->term_abi || manifest->semantic_source_count == 0u ||
        !manifest->program_nil || !manifest->program_cons ||
        !manifest->has_entry || !manifest->observation)
        return language_error(error, error_size,
                              "manifest omits a required stage");
    if (strcmp(manifest->term_abi, "finite-horn-quote-v1") != 0)
        return language_error(error, error_size,
                              "manifest requests an unsupported term ABI");
    if (strcmp(manifest->observation, "bag") != 0)
        return language_error(error, error_size,
                              "manifest requests an unsupported observation");
    return true;
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
        !language_manifest_parse(forms[0], &manifest, error, error_size))
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
    language->syntax_backend = language_strdup(manifest.syntax_backend);
    language->program_nil = language_strdup(manifest.program_nil);
    language->program_cons = language_strdup(manifest.program_cons);
    language->entry_relation = language_strdup(manifest.entry_relation);
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
    if (!descriptor || !out || !descriptor->name ||
        !descriptor->syntax_backend || !descriptor->term_abi ||
        !descriptor->semantic_sources ||
        descriptor->semantic_source_count == 0u ||
        !descriptor->program_nil || !descriptor->program_cons ||
        !descriptor->entry_relation || descriptor->entry_arity == 0u ||
        descriptor->entry_arity == UINT32_MAX ||
        descriptor->program_position >= descriptor->entry_arity ||
        descriptor->result_position >= descriptor->entry_arity ||
        descriptor->program_position == descriptor->result_position ||
        !descriptor->observation || !descriptor->manifest_sha256 ||
        !descriptor->compiler_sha256)
        return language_error(error, error_size,
                              "invalid embedded GSLT language descriptor");
    *out = NULL;
    if (strcmp(descriptor->term_abi, "finite-horn-quote-v1") != 0 ||
        strcmp(descriptor->observation, "bag") != 0)
        return language_error(error, error_size,
                              "unsupported embedded GSLT language contract");
    CettaGsltHornInput *inputs = cetta_malloc(
        sizeof(*inputs) * descriptor->semantic_source_count);
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++) {
        const CettaGsltEmbeddedSemanticSourceV1 *source =
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
    language->syntax_backend = language_strdup(descriptor->syntax_backend);
    language->program_nil = language_strdup(descriptor->program_nil);
    language->program_cons = language_strdup(descriptor->program_cons);
    language->entry_relation = language_strdup(descriptor->entry_relation);
    language->observation = language_strdup(descriptor->observation);
    language->entry_arity = descriptor->entry_arity;
    language->program_position = descriptor->program_position;
    language->result_position = descriptor->result_position;
    bool loaded = cetta_gslt_horn_program_load_inputs(
        inputs, descriptor->semantic_source_count,
        &language->semantics, error, error_size);
    free(inputs);
    if (!loaded) {
        cetta_gslt_language_free(language);
        return false;
    }
    *out = language;
    return true;
}

void cetta_gslt_language_free(CettaGsltLanguage *language) {
    if (!language)
        return;
    cetta_gslt_horn_program_free(language->semantics);
    free(language->name);
    free(language->syntax_backend);
    free(language->program_nil);
    free(language->program_cons);
    free(language->entry_relation);
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
    return language
        ? cetta_gslt_horn_program_rule_count(language->semantics) : 0u;
}

static Atom *language_program(
    const CettaGsltLanguage *language,
    Atom *const *forms, size_t form_count, Arena *arena) {
    if (form_count > (size_t)INT64_MAX)
        return NULL;
    Atom *program = atom_symbol(arena, language->program_nil);
    for (size_t index = form_count; index > 0u; index--) {
        Atom *quoted = cetta_gslt_quote_atom_v1(arena, forms[index - 1u]);
        Atom *occurrence = atom_expr2(
            arena, atom_symbol(arena, "gslt-source-occurrence"),
            atom_int(arena, (int64_t)(index - 1u)));
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

bool cetta_gslt_language_execute_atoms(
    const CettaGsltLanguage *language,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size) {
    if (!language || (!forms && form_count > 0u) || !output_arena || !result)
        return language_error(error, error_size,
                              "invalid GSLT language execution request");
    memset(result, 0, sizeof(*result));
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
    CettaGsltHornResult horn;
    if (!cetta_gslt_horn_query(
            language->semantics, output_arena, query, limits,
            &horn, error, error_size))
        return false;
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
    memset(result, 0, sizeof(*result));
}
