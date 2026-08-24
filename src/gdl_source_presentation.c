#include "gdl_source_presentation.h"

#include "native_sha256.h"
#include "symbol.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *cursor;
    size_t line;
    size_t max_depth;
    Arena *arena;
    GdlSourceParseV1 status;
} GdlSourceParserV1;

static bool gdl_source_expr_named_v1(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom_is_symbol(atom->expr.elems[0], name);
}

static bool gdl_source_string_v1(const Atom *atom, const char **text_out) {
    if (!atom || !text_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_STRING || !atom->ground.sval)
        return false;
    *text_out = atom->ground.sval;
    return true;
}

static bool gdl_source_hex_digest_v1(const char *text) {
    size_t index;
    if (!text || strlen(text) != 64u)
        return false;
    for (index = 0u; index < 64u; index++)
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
            return false;
    return true;
}

static void gdl_source_sha_length_v1(
    CettaNativeSha256 *sha, size_t length) {
    uint8_t bytes[8];
    size_t index;
    uint64_t value = (uint64_t)length;
    for (index = 0u; index < sizeof(bytes); index++)
        bytes[sizeof(bytes) - 1u - index] =
            (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void gdl_source_revision_v1(
    const char *source, const char *profile, char revision[81],
    char digest_out[65]) {
    static const uint8_t domain[] = "cetta.gdl-type-source.v1\0";
    CettaNativeSha256 sha;
    size_t source_length = strlen(source);
    size_t profile_length = strlen(profile);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    gdl_source_sha_length_v1(&sha, source_length);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)source, source_length);
    gdl_source_sha_length_v1(&sha, profile_length);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)profile, profile_length);
    cetta_native_sha256_finish_hex(&sha, digest_out);
    snprintf(revision, 81u, "gdl-type-source-%s", digest_out);
}

GdlSourceParseV1 gdl_source_package_view_v1(
    Atom *source_program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    GdlSourcePackageV1 *package_out) {
    const char *declared_source;
    const char *declared_profile;
    const char *declared_revision;
    const char *source;
    const char *profile;
    bool pinned = expected_source_sha256 || expected_profile_sha256 ||
        expected_revision;
    if (package_out)
        memset(package_out, 0, sizeof(*package_out));
    if (!source_program || !package_out || !g_symbols ||
        (pinned && (!expected_source_sha256 || !expected_profile_sha256 ||
                    !expected_revision)))
        return GDL_SOURCE_PARSE_ENGINE_FAULT_V1;
    if ((pinned &&
         (!gdl_source_hex_digest_v1(expected_source_sha256) ||
          !gdl_source_hex_digest_v1(expected_profile_sha256))) ||
        !gdl_source_expr_named_v1(
            source_program, "gdl-type-source-v1", 6u))
        return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;

    Atom *source_digest_field = source_program->expr.elems[1];
    Atom *profile_digest_field = source_program->expr.elems[2];
    Atom *revision_field = source_program->expr.elems[3];
    Atom *source_text_field = source_program->expr.elems[4];
    Atom *profile_text_field = source_program->expr.elems[5];
    if (!gdl_source_expr_named_v1(
            source_digest_field, "source-digest", 2u) ||
        !gdl_source_string_v1(
            source_digest_field->expr.elems[1], &declared_source) ||
        !gdl_source_expr_named_v1(
            profile_digest_field, "profile-digest", 2u) ||
        !gdl_source_string_v1(
            profile_digest_field->expr.elems[1], &declared_profile) ||
        !gdl_source_expr_named_v1(revision_field, "revision", 2u) ||
        revision_field->expr.elems[1]->kind != ATOM_SYMBOL ||
        !(declared_revision = symbol_bytes(
            g_symbols, revision_field->expr.elems[1]->sym_id)) ||
        !gdl_source_expr_named_v1(source_text_field, "source-text", 2u) ||
        !gdl_source_string_v1(source_text_field->expr.elems[1], &source) ||
        !gdl_source_expr_named_v1(profile_text_field, "profile-text", 2u) ||
        !gdl_source_string_v1(profile_text_field->expr.elems[1], &profile))
        return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;

    cetta_native_sha256_hex(
        (const uint8_t *)source, strlen(source), package_out->source_sha256);
    cetta_native_sha256_hex(
        (const uint8_t *)profile, strlen(profile),
        package_out->profile_sha256);
    gdl_source_revision_v1(
        source, profile, package_out->revision,
        package_out->calculus_input_sha256);
    if (strcmp(declared_source, package_out->source_sha256) != 0 ||
        strcmp(declared_profile, package_out->profile_sha256) != 0 ||
        strcmp(declared_revision, package_out->revision) != 0 ||
        (pinned &&
         (strcmp(expected_source_sha256, package_out->source_sha256) != 0 ||
          strcmp(expected_profile_sha256, package_out->profile_sha256) != 0 ||
          strcmp(expected_revision, package_out->revision) != 0))) {
        memset(package_out, 0, sizeof(*package_out));
        return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
    }
    package_out->source_text = source;
    package_out->profile_text = profile;
    return GDL_SOURCE_PARSE_OK_V1;
}

static bool gdl_source_reserve_v1(
    void **items, size_t *capacity, size_t needed, size_t item_size) {
    size_t next;
    if (!items || !capacity || item_size == 0u)
        return false;
    if (*capacity >= needed)
        return true;
    next = *capacity ? *capacity : 16u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / item_size)
        return false;
    *items = cetta_realloc(*items, next * item_size);
    *capacity = next;
    return true;
}

static void gdl_source_skip_comment_v1(GdlSourceParserV1 *parser) {
    while (*parser->cursor && *parser->cursor != '\n')
        parser->cursor++;
}

static void gdl_source_skip_space_v1(GdlSourceParserV1 *parser) {
    bool again = true;
    while (again) {
        again = false;
        while (*parser->cursor && isspace((unsigned char)*parser->cursor)) {
            if (*parser->cursor == '\n')
                parser->line++;
            parser->cursor++;
        }
        if (*parser->cursor == ';' || *parser->cursor == '%') {
            gdl_source_skip_comment_v1(parser);
            again = true;
        }
    }
}

static GdlSourceRawExprV1 *gdl_source_parse_expr_v1(
    GdlSourceParserV1 *parser, size_t depth) {
    GdlSourceRawExprV1 *result;
    if (!parser || parser->status != GDL_SOURCE_PARSE_OK_V1)
        return NULL;
    if (depth >= parser->max_depth) {
        parser->status = GDL_SOURCE_PARSE_INCOMPLETE_V1;
        return NULL;
    }
    gdl_source_skip_space_v1(parser);
    if (!*parser->cursor || *parser->cursor == ')') {
        parser->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
        return NULL;
    }
    result = arena_alloc(parser->arena, sizeof(*result));
    memset(result, 0, sizeof(*result));
    if (*parser->cursor != '(') {
        const char *start = parser->cursor;
        size_t length;
        char *token;
        while (*parser->cursor && !isspace((unsigned char)*parser->cursor) &&
               *parser->cursor != '(' && *parser->cursor != ')' &&
               *parser->cursor != ';')
            parser->cursor++;
        length = (size_t)(parser->cursor - start);
        if (length == 0u) {
            parser->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
            return NULL;
        }
        token = arena_alloc(parser->arena, length + 1u);
        memcpy(token, start, length);
        token[length] = '\0';
        result->token = token;
        return result;
    }
    parser->cursor++;
    {
        GdlSourceRawExprV1 **items = NULL;
        size_t count = 0u;
        size_t capacity = 0u;
        for (;;) {
            GdlSourceRawExprV1 *item;
            gdl_source_skip_space_v1(parser);
            if (!*parser->cursor) {
                free(items);
                parser->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
                return NULL;
            }
            if (*parser->cursor == ')') {
                parser->cursor++;
                break;
            }
            item = gdl_source_parse_expr_v1(parser, depth + 1u);
            if (!item || !gdl_source_reserve_v1(
                    (void **)&items, &capacity, count + 1u,
                    sizeof(*items))) {
                free(items);
                if (parser->status == GDL_SOURCE_PARSE_OK_V1)
                    parser->status = GDL_SOURCE_PARSE_INCOMPLETE_V1;
                return NULL;
            }
            items[count++] = item;
        }
        if (count == 0u) {
            free(items);
            parser->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
            return NULL;
        }
        result->items = arena_alloc(parser->arena, count * sizeof(*items));
        memcpy(result->items, items, count * sizeof(*items));
        result->count = count;
        free(items);
    }
    return result;
}

GdlSourceParseV1 gdl_source_parse_forms_v1(
    Arena *arena, const char *source, size_t max_depth,
    GdlSourceRawFormsV1 *forms_out) {
    GdlSourceParserV1 parser = {
        .cursor = source,
        .line = 1u,
        .max_depth = max_depth,
        .arena = arena,
        .status = GDL_SOURCE_PARSE_OK_V1,
    };
    if (!arena || !source || !forms_out || max_depth == 0u)
        return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
    memset(forms_out, 0, sizeof(*forms_out));
    while (*parser.cursor) {
        GdlSourceRawFormV1 form;
        gdl_source_skip_space_v1(&parser);
        if (!*parser.cursor)
            break;
        if (*parser.cursor != '(') {
            forms_out->foreign_lines++;
            gdl_source_skip_comment_v1(&parser);
            continue;
        }
        form.start_line = parser.line;
        form.form = gdl_source_parse_expr_v1(&parser, 0u);
        form.end_line = parser.line;
        form.selected = true;
        if (!form.form || form.form->token || form.form->count == 0u ||
            !form.form->items[0]->token)
            goto fail;
        if (!gdl_source_reserve_v1(
                (void **)&forms_out->items, &forms_out->capacity,
                forms_out->count + 1u, sizeof(*forms_out->items))) {
            parser.status = GDL_SOURCE_PARSE_INCOMPLETE_V1;
            goto fail;
        }
        forms_out->items[forms_out->count++] = form;
    }
    if (forms_out->count == 0u)
        parser.status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
fail:
    if (parser.status != GDL_SOURCE_PARSE_OK_V1)
        gdl_source_raw_forms_free_v1(forms_out);
    return parser.status;
}

typedef struct {
    const char *name;
    size_t arity;
    bool defined;
} GdlSourceTargetRelationV1;

typedef struct {
    GdlSourceRawFormsV1 *forms;
    GdlSourceTargetRelationV1 *relations;
    size_t relation_count;
    size_t relation_capacity;
    size_t max_relations;
    size_t max_logical_depth;
    GdlSourceParseV1 status;
} GdlSourceTargetSliceBuilderV1;

static bool gdl_source_raw_head_v1(
    const GdlSourceRawExprV1 *expression,
    const char *name) {
    return expression && !expression->token && expression->count > 0u &&
        expression->items[0] && expression->items[0]->token &&
        strcmp(expression->items[0]->token, name) == 0;
}

static bool gdl_source_relation_signature_v1(
    const GdlSourceRawExprV1 *expression,
    const char **name_out,
    size_t *arity_out) {
    const char *name = NULL;
    size_t arity = 0u;
    if (!expression || !name_out || !arity_out)
        return false;
    if (expression->token) {
        name = expression->token;
    } else if (expression->count > 0u && expression->items[0] &&
               expression->items[0]->token) {
        name = expression->items[0]->token;
        arity = expression->count - 1u;
    }
    if (!name || name[0] == '?' || name[0] == '\0')
        return false;
    *name_out = name;
    *arity_out = arity;
    return true;
}

static bool gdl_source_target_relation_add_v1(
    GdlSourceTargetSliceBuilderV1 *builder,
    const char *name,
    size_t arity) {
    if (!builder || builder->status != GDL_SOURCE_PARSE_OK_V1 || !name ||
        name[0] == '?' || name[0] == '\0')
        return false;
    for (size_t index = 0u; index < builder->relation_count; index++)
        if (builder->relations[index].arity == arity &&
            strcmp(builder->relations[index].name, name) == 0)
            return true;
    if (builder->relation_count == builder->max_relations ||
        !gdl_source_reserve_v1(
            (void **)&builder->relations,
            &builder->relation_capacity,
            builder->relation_count + 1u,
            sizeof(*builder->relations))) {
        builder->status = GDL_SOURCE_PARSE_INCOMPLETE_V1;
        return false;
    }
    builder->relations[builder->relation_count++] =
        (GdlSourceTargetRelationV1){
            .name = name,
            .arity = arity,
        };
    return true;
}

static bool gdl_source_target_dependencies_v1(
    GdlSourceTargetSliceBuilderV1 *builder,
    const GdlSourceRawExprV1 *expression,
    size_t depth) {
    if (!builder || builder->status != GDL_SOURCE_PARSE_OK_V1 ||
        !expression)
        return false;
    if (depth > builder->max_logical_depth) {
        builder->status = GDL_SOURCE_PARSE_INCOMPLETE_V1;
        return false;
    }
    if (gdl_source_raw_head_v1(expression, "distinct")) {
        if (expression->count != 3u)
            builder->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
        return builder->status == GDL_SOURCE_PARSE_OK_V1;
    }
    bool negation = gdl_source_raw_head_v1(expression, "not");
    bool conjunction = gdl_source_raw_head_v1(expression, "and");
    bool disjunction = gdl_source_raw_head_v1(expression, "or");
    if (negation || conjunction || disjunction) {
        if ((negation && expression->count != 2u) ||
            (conjunction && expression->count < 2u) ||
            (disjunction && expression->count < 3u)) {
            builder->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
            return false;
        }
        for (size_t index = 1u; index < expression->count; index++)
            if (!gdl_source_target_dependencies_v1(
                    builder, expression->items[index], depth + 1u))
                return false;
        return true;
    }
    const char *name = NULL;
    size_t arity = 0u;
    if (!gdl_source_relation_signature_v1(expression, &name, &arity)) {
        builder->status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
        return false;
    }
    return gdl_source_target_relation_add_v1(builder, name, arity);
}

GdlSourceParseV1 gdl_source_select_target_dependency_v1(
    GdlSourceRawFormsV1 *forms,
    const char *target_name,
    size_t target_arity,
    size_t max_relations,
    size_t max_logical_depth,
    GdlSourceTargetSliceV1 *slice_out) {
    if (slice_out)
        memset(slice_out, 0, sizeof(*slice_out));
    if (!forms || !target_name || target_name[0] == '\0' ||
        target_name[0] == '?' || !slice_out)
        return GDL_SOURCE_PARSE_ENGINE_FAULT_V1;
    if (forms->foreign_lines != 0u)
        return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
    if (max_relations == 0u)
        max_relations = 65536u;
    if (max_logical_depth == 0u)
        max_logical_depth = 4096u;
    for (size_t index = 0u; index < forms->count; index++)
        forms->items[index].selected = false;

    GdlSourceTargetSliceBuilderV1 builder = {
        .forms = forms,
        .max_relations = max_relations,
        .max_logical_depth = max_logical_depth,
        .status = GDL_SOURCE_PARSE_OK_V1,
    };
    if (!gdl_source_target_relation_add_v1(
            &builder, target_name, target_arity)) {
        free(builder.relations);
        return builder.status;
    }

    for (size_t relation_index = 0u;
         relation_index < builder.relation_count &&
         builder.status == GDL_SOURCE_PARSE_OK_V1;
         relation_index++) {
        GdlSourceTargetRelationV1 *wanted =
            &builder.relations[relation_index];
        for (size_t form_index = 0u; form_index < forms->count;
             form_index++) {
            GdlSourceRawFormV1 *source = &forms->items[form_index];
            GdlSourceRawExprV1 *form = source->form;
            if (!form) {
                builder.status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
                break;
            }
            if (gdl_source_raw_head_v1(form, "distinct")) {
                if (form->count != 3u)
                    builder.status =
                        GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
                continue;
            }
            bool rule = gdl_source_raw_head_v1(form, "<=");
            if (rule && form->count < 2u) {
                builder.status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
                break;
            }
            GdlSourceRawExprV1 *head = rule ? form->items[1] : form;
            const char *head_name = NULL;
            size_t head_arity = 0u;
            if (!gdl_source_relation_signature_v1(
                    head, &head_name, &head_arity)) {
                builder.status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
                break;
            }
            if (head_arity != wanted->arity ||
                strcmp(head_name, wanted->name) != 0)
                continue;
            wanted->defined = true;
            source->selected = true;
            if (!rule)
                continue;
            for (size_t premise = 2u; premise < form->count; premise++)
                if (!gdl_source_target_dependencies_v1(
                        &builder, form->items[premise], 1u))
                    break;
            if (builder.status != GDL_SOURCE_PARSE_OK_V1)
                break;
        }
    }

    GdlSourceParseV1 status = builder.status;
    if (status == GDL_SOURCE_PARSE_OK_V1 &&
        (builder.relation_count == 0u || !builder.relations[0].defined))
        status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
    if (status == GDL_SOURCE_PARSE_OK_V1) {
        size_t external = 0u;
        for (size_t index = 0u; index < builder.relation_count; index++)
            external += !builder.relations[index].defined;
        *slice_out = (GdlSourceTargetSliceV1){
            .target_name = target_name,
            .target_arity = target_arity,
            .source_forms = forms->count,
            .selected_forms = gdl_source_selected_form_count_v1(forms),
            .reachable_relations = builder.relation_count,
            .external_relations = external,
        };
        if (slice_out->selected_forms == 0u)
            status = GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
    }
    free(builder.relations);
    if (status != GDL_SOURCE_PARSE_OK_V1)
        memset(slice_out, 0, sizeof(*slice_out));
    return status;
}

size_t gdl_source_selected_form_count_v1(
    const GdlSourceRawFormsV1 *forms) {
    size_t selected = 0u;
    if (!forms)
        return 0u;
    for (size_t index = 0u; index < forms->count; index++)
        selected += forms->items[index].selected;
    return selected;
}

bool gdl_source_target_calculus_input_v1(
    const char *source_calculus_input_sha256,
    const GdlSourceRawFormsV1 *forms,
    const char *target_name,
    size_t target_arity,
    char digest_out[65]) {
    static const char domain[] =
        "cetta.gdl-source-target-calculus-input.v1";
    if (!gdl_source_hex_digest_v1(source_calculus_input_sha256) || !forms ||
        !target_name || target_name[0] == '\0' || !digest_out)
        return false;
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain));
    gdl_source_sha_length_v1(&sha, strlen(source_calculus_input_sha256));
    cetta_native_sha256_update(
        &sha, (const uint8_t *)source_calculus_input_sha256,
        strlen(source_calculus_input_sha256));
    gdl_source_sha_length_v1(&sha, strlen(target_name));
    cetta_native_sha256_update(
        &sha, (const uint8_t *)target_name, strlen(target_name));
    gdl_source_sha_length_v1(&sha, target_arity);
    size_t selected = gdl_source_selected_form_count_v1(forms);
    gdl_source_sha_length_v1(&sha, selected);
    for (size_t index = 0u; index < forms->count; index++)
        if (forms->items[index].selected)
            gdl_source_sha_length_v1(&sha, index + 1u);
    cetta_native_sha256_finish_hex(&sha, digest_out);
    return true;
}

static bool gdl_source_profile_symbol_v1(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (!cursor || !*cursor)
        return false;
    if (*cursor == '-') {
        cursor++;
        if (!isdigit(*cursor))
            return false;
        while (isdigit(*cursor))
            cursor++;
        return *cursor == '\0';
    }
    if (isdigit(*cursor)) {
        while (isdigit(*cursor))
            cursor++;
        return *cursor == '\0';
    }
    if (!(isalpha(*cursor) || *cursor == '_'))
        return false;
    cursor++;
    while (isalnum(*cursor) || *cursor == '_')
        cursor++;
    return *cursor == '\0';
}

static char *gdl_source_trim_v1(
    Arena *arena, const char *start, size_t length) {
    char *result;
    while (length > 0u && isspace((unsigned char)*start)) {
        start++;
        length--;
    }
    while (length > 0u && isspace((unsigned char)start[length - 1u]))
        length--;
    if (length == 0u)
        return NULL;
    result = arena_alloc(arena, length + 1u);
    memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

static bool gdl_source_split_tokens_v1(
    Arena *arena, char *text, const char *separator,
    const char ***items_out, size_t *count_out) {
    const char **items = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    size_t separator_length = strlen(separator);
    char *cursor = text;
    if (!arena || !text || !separator || separator_length == 0u ||
        !items_out || !count_out)
        return false;
    for (;;) {
        char *next = strstr(cursor, separator);
        char *item = gdl_source_trim_v1(
            arena, cursor, next ? (size_t)(next - cursor) : strlen(cursor));
        if (!item || !gdl_source_profile_symbol_v1(item) ||
            !gdl_source_reserve_v1(
                (void **)&items, &capacity, count + 1u, sizeof(*items))) {
            free(items);
            return false;
        }
        items[count++] = item;
        if (!next)
            break;
        cursor = next + separator_length;
    }
    *items_out = arena_alloc(arena, count * sizeof(*items));
    memcpy((void *)*items_out, items, count * sizeof(*items));
    *count_out = count;
    free(items);
    return true;
}

GdlSourceParseV1 gdl_source_parse_profile_v1(
    Arena *arena, const char *profile, GdlSourceProfileV1 *profile_out) {
    size_t length;
    char *clean;
    size_t source_index = 0u;
    size_t clean_index = 0u;
    char *statement_start;
    if (!arena || !profile || !profile_out)
        return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
    memset(profile_out, 0, sizeof(*profile_out));
    length = strlen(profile);
    clean = arena_alloc(arena, length + 1u);
    while (source_index < length) {
        char character = profile[source_index++];
        if (character == '%' || character == ';') {
            while (source_index < length && profile[source_index] != '\n')
                source_index++;
            continue;
        }
        clean[clean_index++] = character;
    }
    clean[clean_index] = '\0';
    statement_start = clean;
    for (;;) {
        char *period = strchr(statement_start, '.');
        char *statement;
        char *signature;
        char *subtype;
        if (!period)
            break;
        statement = gdl_source_trim_v1(
            arena, statement_start, (size_t)(period - statement_start));
        statement_start = period + 1u;
        if (!statement)
            continue;
        signature = strstr(statement, "::");
        subtype = strstr(statement, ":>");
        if ((signature != NULL) == (subtype != NULL))
            goto malformed;
        if (signature) {
            const char **names;
            const char **types;
            size_t name_count;
            size_t type_count;
            size_t name_index;
            *signature = '\0';
            if (strstr(signature + 2u, "::") ||
                strstr(signature + 2u, ":>") ||
                !gdl_source_split_tokens_v1(
                    arena, statement, ",", &names, &name_count) ||
                !gdl_source_split_tokens_v1(
                    arena, signature + 2u, "->", &types, &type_count) ||
                type_count == 0u)
                goto malformed;
            for (name_index = 0u; name_index < name_count; name_index++) {
                GdlSourceSignatureV1 *item;
                if (!gdl_source_reserve_v1(
                        (void **)&profile_out->signatures,
                        &profile_out->signature_capacity,
                        profile_out->signature_count + 1u,
                        sizeof(*profile_out->signatures)))
                    goto incomplete;
                item = &profile_out->signatures[
                    profile_out->signature_count++];
                item->statement_ordinal = profile_out->statement_count;
                item->name_ordinal = name_index;
                item->name = names[name_index];
                item->argument_types = types;
                item->argument_count = type_count - 1u;
                item->result_type = types[type_count - 1u];
            }
        } else {
            GdlSourceSubtypeV1 *item;
            char *left;
            char *right;
            *subtype = '\0';
            if (strstr(subtype + 2u, "::") ||
                strstr(subtype + 2u, ":>") ||
                !(left = gdl_source_trim_v1(
                    arena, statement, strlen(statement))) ||
                !(right = gdl_source_trim_v1(
                    arena, subtype + 2u, strlen(subtype + 2u))) ||
                !gdl_source_profile_symbol_v1(left) ||
                !gdl_source_profile_symbol_v1(right))
                goto malformed;
            if (!gdl_source_reserve_v1(
                    (void **)&profile_out->subtypes,
                    &profile_out->subtype_capacity,
                    profile_out->subtype_count + 1u,
                    sizeof(*profile_out->subtypes)))
                goto incomplete;
            item = &profile_out->subtypes[profile_out->subtype_count++];
            item->statement_ordinal = profile_out->statement_count;
            item->subtype = left;
            item->supertype = right;
        }
        profile_out->statement_count++;
    }
    while (*statement_start && isspace((unsigned char)*statement_start))
        statement_start++;
    if (*statement_start || profile_out->statement_count == 0u)
        goto malformed;
    return GDL_SOURCE_PARSE_OK_V1;

incomplete:
    gdl_source_profile_free_v1(profile_out);
    return GDL_SOURCE_PARSE_INCOMPLETE_V1;
malformed:
    gdl_source_profile_free_v1(profile_out);
    return GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1;
}

void gdl_source_raw_forms_free_v1(GdlSourceRawFormsV1 *forms) {
    if (!forms)
        return;
    free(forms->items);
    memset(forms, 0, sizeof(*forms));
}

void gdl_source_profile_free_v1(GdlSourceProfileV1 *profile) {
    if (!profile)
        return;
    free(profile->signatures);
    free(profile->subtypes);
    memset(profile, 0, sizeof(*profile));
}
