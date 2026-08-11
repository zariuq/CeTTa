#include "gslt_support_transform_runtime.h"

#include "gslt_pure_provider_v1.h"
#include "match.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} MorkCompactKeyV1;

typedef struct {
    Atom *atom;
    bool present;
    MorkCompactKeyV1 key;
} SupportEntryV1;

typedef struct {
    SupportEntryV1 *entries;
    size_t len;
    size_t cap;
    Arena *arena;
} SupportSpaceV1;

typedef struct {
    Bindings *items;
    size_t len;
    size_t cap;
} MatchRowsV1;

typedef enum {
    SOURCE_FACTOR_BTM_V1 = 0,
    SOURCE_FACTOR_EQUAL_V1 = 1,
    SOURCE_FACTOR_NOT_EQUAL_V1 = 2,
} SourceFactorKindV1;

typedef enum {
    SINK_ADD_V1 = 0,
    SINK_REMOVE_V1 = 1,
    SINK_HEAD_V1 = 2,
    SINK_TAIL_V1 = 3,
    SINK_GROUP_CARDINALITY_V1 = 4,
    SINK_EVALUATE_PROJECT_PURE_F64_V1 = 5,
} SinkKindV1;

typedef struct {
    bool explicit_sources;
    Atom *input;
} InputViewV1;

typedef struct {
    bool explicit_sinks;
    Atom *output;
} OutputViewV1;

typedef struct {
    size_t entry_index;
    InputViewV1 input;
    OutputViewV1 output;
} DirectiveViewV1;

typedef struct {
    SinkKindV1 kind;
    const CettaGsltSupportOperatorDeclV1 *declaration;
    Atom *body;
    Atom *group_counter;
    Atom *group_witness;
    Atom *evaluation_pattern;
    Atom *evaluation_call;
    uint64_t limit;
    SupportSpaceV1 staged;
} StagedSinkV1;

static bool runtime_error(char *error, size_t error_size,
                          const char *format, ...) {
    if (error && error_size > 0u) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, error_size, format, args);
        va_end(args);
    }
    return false;
}

static bool text_present(const char *text) {
    return text && text[0] != '\0';
}

static bool sha256_text(const char *text) {
    if (!text || strlen(text) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        char ch = text[index];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return false;
    }
    return true;
}

static bool operator_id_is_source_v1(const char *operator_id) {
    return strcmp(operator_id, "support.snapshot-match.v1") == 0 ||
        strcmp(operator_id, "support.equal.v1") == 0 ||
        strcmp(operator_id, "support.not-equal.v1") == 0;
}

static bool operator_id_is_sink_v1(const char *operator_id) {
    return strcmp(operator_id, "support.add.v1") == 0 ||
        strcmp(operator_id, "support.remove.v1") == 0 ||
        strcmp(operator_id, "support.head.v1") == 0 ||
        strcmp(operator_id, "support.tail.v1") == 0 ||
        strcmp(operator_id, "support.group-cardinality.v1") == 0 ||
        strcmp(operator_id,
               "support.evaluate-project.mm2-pure-f64.v1") == 0;
}

static bool declarations_validate_v1(
    const CettaGsltSupportOperatorDeclV1 *declarations,
    size_t declaration_count, const char *kind,
    char *error, size_t error_size) {
    if (declaration_count > 0u && !declarations)
        return runtime_error(error, error_size,
                             "support-transform %s declarations are missing",
                             kind);
    for (size_t index = 0u; index < declaration_count; index++) {
        const CettaGsltSupportOperatorDeclV1 *declaration =
            &declarations[index];
        if (!text_present(declaration->surface_symbol) ||
            !text_present(declaration->operator_id))
            return runtime_error(error, error_size,
                                 "support-transform %s declaration is empty",
                                 kind);
        if (declaration->argument_count > UINT8_MAX)
            return runtime_error(error, error_size,
                                 "support-transform %s arity is invalid", kind);
        for (size_t prior = 0u; prior < index; prior++) {
            if (strcmp(declarations[prior].surface_symbol,
                       declaration->surface_symbol) == 0)
                return runtime_error(
                    error, error_size,
                    "support-transform %s surface is declared twice: %s",
                    kind, declaration->surface_symbol);
        }
    }
    return true;
}

bool cetta_gslt_support_transform_profile_validate_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size) {
    if (!profile)
        return runtime_error(error, error_size, "missing support-transform profile");
    if (profile->abi_version != 1u)
        return runtime_error(error, error_size,
                             "unsupported support-transform ABI version %u",
                             profile->abi_version);
    const char *const required[] = {
        profile->language_name,
        profile->profile_name,
        profile->manifest_sha256,
        profile->compiler_sha256,
        profile->work_symbol,
        profile->compat_input_symbol,
        profile->compat_input_operator_id,
        profile->explicit_input_symbol,
        profile->compat_output_symbol,
        profile->compat_output_operator_id,
        profile->explicit_output_symbol,
    };
    for (size_t index = 0u;
         index < sizeof(required) / sizeof(required[0]); index++) {
        if (!text_present(required[index]))
            return runtime_error(error, error_size,
                                 "support-transform profile has an empty field");
    }
    if (!sha256_text(profile->manifest_sha256) ||
        !sha256_text(profile->compiler_sha256))
        return runtime_error(error, error_size,
                             "support-transform profile digest is malformed");
    if (profile->work_arity != 3u)
        return runtime_error(error, error_size,
                             "support-transform V1 requires work arity 3");
    const uint32_t positions[] = {
        profile->location_position,
        profile->input_position,
        profile->output_position,
    };
    bool seen[3] = {false, false, false};
    for (size_t index = 0u; index < 3u; index++) {
        if (positions[index] >= 3u || seen[positions[index]])
            return runtime_error(error, error_size,
                                 "support-transform work positions are invalid");
        seen[positions[index]] = true;
    }
    if (profile->scheduler !=
            CETTA_GSLT_SUPPORT_SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION_KEY_V1)
        return runtime_error(error, error_size,
                             "unsupported support-transform scheduler");
    if (profile->unsupported_policy !=
            CETTA_GSLT_SUPPORT_UNSUPPORTED_LEAVE_INERT)
        return runtime_error(error, error_size,
                             "unsupported support-transform unknown-work policy");
    if (strcmp(profile->compat_input_operator_id,
               "support.snapshot-match.v1") != 0 ||
        strcmp(profile->compat_output_operator_id,
               "support.add.v1") != 0)
        return runtime_error(error, error_size,
                             "unsupported compatibility operator identity");
    if (!declarations_validate_v1(
            profile->source_declarations, profile->source_declaration_count,
            "source", error, error_size) ||
        !declarations_validate_v1(
            profile->sink_declarations, profile->sink_declaration_count,
            "sink", error, error_size))
        return false;
    if (!profile->physical_profile_packet ||
        profile->physical_profile_packet_size == 0u)
        return runtime_error(error, error_size,
                             "support-transform physical profile is missing");
    return true;
}

static void key_free(MorkCompactKeyV1 *key) {
    if (!key)
        return;
    free(key->bytes);
    memset(key, 0, sizeof(*key));
}

static bool key_append(MorkCompactKeyV1 *key,
                       const uint8_t *bytes, size_t length) {
    if (length > SIZE_MAX - key->len)
        return false;
    size_t needed = key->len + length;
    if (needed > key->cap) {
        size_t next = key->cap ? key->cap : 32u;
        while (next < needed) {
            if (next > SIZE_MAX / 2u) {
                next = needed;
                break;
            }
            next *= 2u;
        }
        key->bytes = cetta_realloc(key->bytes, next);
        key->cap = next;
    }
    if (length > 0u)
        memcpy(key->bytes + key->len, bytes, length);
    key->len = needed;
    return true;
}

static bool key_push_byte(MorkCompactKeyV1 *key, uint8_t byte) {
    return key_append(key, &byte, 1u);
}

static bool key_push_inline_symbol(MorkCompactKeyV1 *key,
                                   const char *text) {
    size_t length = text ? strlen(text) : 0u;
    if (length == 0u || length >= 64u)
        return false;
    uint8_t tag = (uint8_t)(0xc0u | length);
    return key_push_byte(key, tag) &&
        key_append(key, (const uint8_t *)text, length);
}

static bool key_build(Arena *arena, Atom *root, MorkCompactKeyV1 *key) {
    Atom *inline_stack[64];
    Atom **stack = inline_stack;
    size_t stack_len = 0u;
    size_t stack_cap = sizeof(inline_stack) / sizeof(inline_stack[0]);
    VarId inline_variables[32];
    VarId *variables = inline_variables;
    size_t variable_len = 0u;
    size_t variable_cap =
        sizeof(inline_variables) / sizeof(inline_variables[0]);
    memset(key, 0, sizeof(*key));
    if (!root)
        return false;
    stack[stack_len++] = root;
    while (stack_len > 0u) {
        Atom *atom = stack[--stack_len];
        if (!atom)
            goto failed;
        switch (atom->kind) {
        case ATOM_EXPR:
            if (atom->expr.len >= 64u ||
                !key_push_byte(key, (uint8_t)atom->expr.len))
                goto failed;
            if (!cetta_expr_len_fits_size(atom->expr.len) ||
                (size_t)atom->expr.len > SIZE_MAX - stack_len)
                goto failed;
            if (stack_len + (size_t)atom->expr.len > stack_cap) {
                size_t needed = stack_len + (size_t)atom->expr.len;
                size_t next = stack_cap;
                while (next < needed) {
                    if (next > SIZE_MAX / 2u) {
                        next = needed;
                        break;
                    }
                    next *= 2u;
                }
                if (next > SIZE_MAX / sizeof(*stack))
                    goto failed;
                if (stack == inline_stack) {
                    Atom **grown = cetta_malloc(next * sizeof(*grown));
                    memcpy(grown, inline_stack, stack_len * sizeof(*grown));
                    stack = grown;
                } else {
                    stack = cetta_realloc(stack, next * sizeof(*stack));
                }
                stack_cap = next;
            }
            for (CettaExprIndex index = atom->expr.len; index > 0u; index--)
                stack[stack_len++] = atom->expr.elems[index - 1u];
            break;
        case ATOM_SYMBOL:
            if (!key_push_inline_symbol(key, atom_name_cstr(atom)))
                goto failed;
            break;
        case ATOM_GROUNDED: {
            const char *text = atom_to_parseable_string(arena, atom);
            if (!key_push_inline_symbol(key, text))
                goto failed;
            break;
        }
        case ATOM_VAR: {
            size_t slot = 0u;
            while (slot < variable_len &&
                   variables[slot] != atom->var_id)
                slot++;
            bool introduction = slot == variable_len;
            if (introduction) {
                if (variable_len == variable_cap) {
                    size_t next = variable_cap * 2u;
                    if (next < variable_cap ||
                        next > SIZE_MAX / sizeof(*variables))
                        goto failed;
                    if (variables == inline_variables) {
                        VarId *grown = cetta_malloc(next * sizeof(*grown));
                        memcpy(grown, inline_variables,
                               variable_len * sizeof(*grown));
                        variables = grown;
                    } else {
                        variables = cetta_realloc(
                            variables, next * sizeof(*variables));
                    }
                    variable_cap = next;
                }
                variables[variable_len++] = atom->var_id;
            }
            if (slot >= 64u ||
                !key_push_byte(
                    key, (uint8_t)((introduction ? 0xc0u : 0x80u) | slot)))
                goto failed;
            break;
        }
        }
    }
    if (stack != inline_stack)
        free(stack);
    if (variables != inline_variables)
        free(variables);
    return true;

failed:
    if (stack != inline_stack)
        free(stack);
    if (variables != inline_variables)
        free(variables);
    key_free(key);
    return false;
}

static int key_compare(const MorkCompactKeyV1 *left,
                       const MorkCompactKeyV1 *right) {
    size_t common = left->len < right->len ? left->len : right->len;
    int prefix = common > 0u ? memcmp(left->bytes, right->bytes, common) : 0;
    if (prefix < 0)
        return -1;
    if (prefix > 0)
        return 1;
    if (left->len < right->len)
        return -1;
    if (left->len > right->len)
        return 1;
    return 0;
}

static void support_space_free(SupportSpaceV1 *space) {
    if (!space)
        return;
    for (size_t index = 0u; index < space->len; index++)
        key_free(&space->entries[index].key);
    free(space->entries);
    memset(space, 0, sizeof(*space));
}

static size_t support_find(const SupportSpaceV1 *space, Atom *atom) {
    for (size_t index = 0u; index < space->len; index++) {
        if (atom_eq(space->entries[index].atom, atom))
            return index;
    }
    return SIZE_MAX;
}

static bool support_add(SupportSpaceV1 *space, Atom *atom,
                        char *error, size_t error_size) {
    size_t existing = support_find(space, atom);
    if (existing != SIZE_MAX) {
        space->entries[existing].present = true;
        return true;
    }
    if (space->len == space->cap) {
        size_t next = space->cap ? space->cap * 2u : 32u;
        if (next < space->cap || next > SIZE_MAX / sizeof(*space->entries))
            return runtime_error(error, error_size,
                                 "support-transform space is too large");
        space->entries = cetta_realloc(
            space->entries, next * sizeof(*space->entries));
        space->cap = next;
    }
    SupportEntryV1 *entry = &space->entries[space->len];
    memset(entry, 0, sizeof(*entry));
    entry->atom = atom;
    entry->present = true;
    if (!key_build(space->arena, atom, &entry->key))
        return runtime_error(error, error_size,
                             "cannot construct an inline MORK compact atom key");
    space->len++;
    return true;
}

static void support_remove(SupportSpaceV1 *space, Atom *atom) {
    size_t existing = support_find(space, atom);
    if (existing != SIZE_MAX)
        space->entries[existing].present = false;
}

static bool expr_has_head(Atom *atom, const char *symbol) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom_is_symbol(atom->expr.elems[0], symbol);
}

static const CettaGsltSupportOperatorDeclV1 *find_declaration_v1(
    const CettaGsltSupportOperatorDeclV1 *declarations,
    size_t declaration_count, Atom *form) {
    if (!form || form->kind != ATOM_EXPR || form->expr.len == 0u ||
        form->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    for (size_t index = 0u; index < declaration_count; index++) {
        const CettaGsltSupportOperatorDeclV1 *declaration =
            &declarations[index];
        if (form->expr.len ==
                (CettaExprLen)declaration->argument_count + 1u &&
            atom_is_symbol(form->expr.elems[0],
                           declaration->surface_symbol))
            return declaration;
    }
    return NULL;
}

static bool positive_u64_atom_v1(Atom *atom, uint64_t *value) {
    if (!atom || !value)
        return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT) {
        if (atom->ground.ival <= 0)
            return false;
        *value = (uint64_t)atom->ground.ival;
        return true;
    }
    if (atom->kind != ATOM_SYMBOL)
        return false;
    const char *text = atom_name_cstr(atom);
    if (!text_present(text))
        return false;
    uint64_t parsed = 0u;
    for (size_t index = 0u; text[index] != '\0'; index++) {
        if (text[index] < '0' || text[index] > '9')
            return false;
        uint64_t digit = (uint64_t)(text[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10u)
            return false;
        parsed = parsed * 10u + digit;
    }
    if (parsed == 0u)
        return false;
    *value = parsed;
    return true;
}

static bool nonnegative_u64_atom_v1(Atom *atom, uint64_t *value) {
    if (!atom || !value)
        return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT) {
        if (atom->ground.ival < 0)
            return false;
        *value = (uint64_t)atom->ground.ival;
        return true;
    }
    if (atom->kind != ATOM_SYMBOL)
        return false;
    const char *text = atom_name_cstr(atom);
    if (!text_present(text))
        return false;
    uint64_t parsed = 0u;
    for (size_t index = 0u; text[index] != '\0'; index++) {
        if (text[index] < '0' || text[index] > '9')
            return false;
        uint64_t digit = (uint64_t)(text[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10u)
            return false;
        parsed = parsed * 10u + digit;
    }
    *value = parsed;
    return true;
}

static bool decode_input(
    const CettaGsltSupportTransformProfileV1 *profile, Atom *input,
    InputViewV1 *view) {
    if (expr_has_head(input, profile->compat_input_symbol)) {
        view->explicit_sources = false;
        view->input = input;
        return true;
    }
    if (!expr_has_head(input, profile->explicit_input_symbol))
        return false;
    for (CettaExprIndex index = 1u; index < input->expr.len; index++) {
        const CettaGsltSupportOperatorDeclV1 *declaration =
            find_declaration_v1(
                profile->source_declarations,
                profile->source_declaration_count,
                input->expr.elems[index]);
        if (!declaration ||
            !operator_id_is_source_v1(declaration->operator_id))
            return false;
    }
    view->explicit_sources = true;
    view->input = input;
    return true;
}

static bool decode_output(
    const CettaGsltSupportTransformProfileV1 *profile, Atom *output,
    OutputViewV1 *view) {
    if (expr_has_head(output, profile->compat_output_symbol)) {
        view->explicit_sinks = false;
        view->output = output;
        return true;
    }
    if (!expr_has_head(output, profile->explicit_output_symbol))
        return false;
    for (CettaExprIndex index = 1u; index < output->expr.len; index++) {
        const CettaGsltSupportOperatorDeclV1 *declaration =
            find_declaration_v1(
                profile->sink_declarations,
                profile->sink_declaration_count,
                output->expr.elems[index]);
        if (!declaration ||
            !operator_id_is_sink_v1(declaration->operator_id))
            return false;
        if ((strcmp(declaration->operator_id, "support.head.v1") == 0 ||
             strcmp(declaration->operator_id, "support.tail.v1") == 0)) {
            uint64_t ignored;
            if (!positive_u64_atom_v1(
                    output->expr.elems[index]->expr.elems[1], &ignored))
                return false;
        }
        if (strcmp(declaration->operator_id,
                   "support.group-cardinality.v1") == 0) {
            Atom *counter = output->expr.elems[index]->expr.elems[2];
            uint64_t ignored;
            if (counter->kind != ATOM_VAR &&
                !nonnegative_u64_atom_v1(counter, &ignored))
                return false;
        }
        if (strcmp(declaration->operator_id,
                   "support.evaluate-project.mm2-pure-f64.v1") == 0 &&
            !cetta_gslt_pure_f64_accepts_v1(
                output->expr.elems[index]->expr.elems[3]))
            return false;
    }
    view->explicit_sinks = true;
    view->output = output;
    return true;
}

static bool decode_directive(
    const CettaGsltSupportTransformProfileV1 *profile,
    Atom *atom, size_t entry_index, DirectiveViewV1 *directive) {
    if (!expr_has_head(atom, profile->work_symbol) ||
        atom->expr.len != (CettaExprLen)profile->work_arity + 1u)
        return false;
    Atom *location = atom->expr.elems[profile->location_position + 1u];
    Atom *input = atom->expr.elems[profile->input_position + 1u];
    Atom *output = atom->expr.elems[profile->output_position + 1u];
    InputViewV1 input_view;
    OutputViewV1 output_view;
    if (atom_has_vars(location) ||
        !decode_input(profile, input, &input_view) ||
        !decode_output(profile, output, &output_view))
        return false;
    directive->entry_index = entry_index;
    directive->input = input_view;
    directive->output = output_view;
    return true;
}

static bool select_directive(
    const CettaGsltSupportTransformProfileV1 *profile,
    SupportSpaceV1 *space, bool *found, DirectiveViewV1 *selected,
    char *error, size_t error_size) {
    (void)error;
    (void)error_size;
    *found = false;
    for (size_t index = 0u; index < space->len; index++) {
        if (!space->entries[index].present)
            continue;
        DirectiveViewV1 candidate;
        if (!decode_directive(profile, space->entries[index].atom,
                              index, &candidate))
            continue;
        int order = *found
            ? key_compare(&space->entries[index].key,
                          &space->entries[selected->entry_index].key)
            : -1;
        if (!*found || order < 0) {
            *selected = candidate;
            *found = true;
        }
    }
    return true;
}

static bool support_match_atom(SupportSpaceV1 *space, Atom *pattern,
                               Atom *target, Bindings *bindings) {
    /* MM2 rows are open relational terms, not ground database records.  Each
     * use receives a fresh variable scope, including two factors that reuse
     * the same support atom. */
    Atom *fresh_target = atom_has_vars(target)
        ? rename_vars(space->arena, target, fresh_var_suffix())
        : target;
    return fresh_target && match_atoms(pattern, fresh_target, bindings);
}

static void match_rows_free(MatchRowsV1 *rows) {
    if (!rows)
        return;
    for (size_t index = 0u; index < rows->len; index++)
        bindings_free(&rows->items[index]);
    free(rows->items);
    memset(rows, 0, sizeof(*rows));
}

static bool match_rows_append(MatchRowsV1 *rows, const Bindings *bindings,
                              char *error, size_t error_size) {
    if (rows->len == rows->cap) {
        size_t next = rows->cap ? rows->cap * 2u : 16u;
        if (next < rows->cap || next > SIZE_MAX / sizeof(*rows->items))
            return runtime_error(error, error_size,
                                 "support-transform match bag is too large");
        rows->items = cetta_realloc(rows->items, next * sizeof(*rows->items));
        rows->cap = next;
    }
    bindings_init(&rows->items[rows->len]);
    if (!bindings_clone(&rows->items[rows->len], bindings))
        return runtime_error(error, error_size,
                             "cannot retain a support-transform match");
    rows->len++;
    return true;
}

static size_t *present_indices_sorted(const SupportSpaceV1 *space,
                                      size_t *count) {
    size_t present = 0u;
    for (size_t index = 0u; index < space->len; index++) {
        if (space->entries[index].present)
            present++;
    }
    size_t *indices = present ? cetta_malloc(present * sizeof(*indices)) : NULL;
    size_t length = 0u;
    for (size_t index = 0u; index < space->len; index++) {
        if (!space->entries[index].present)
            continue;
        size_t insertion = length;
        while (insertion > 0u &&
               key_compare(&space->entries[index].key,
                           &space->entries[indices[insertion - 1u]].key) < 0) {
            indices[insertion] = indices[insertion - 1u];
            insertion--;
        }
        indices[insertion] = index;
        length++;
    }
    *count = length;
    return indices;
}

static bool factor_view(
    const CettaGsltSupportTransformProfileV1 *profile,
    const InputViewV1 *input, CettaExprIndex position,
    SourceFactorKindV1 *kind, Atom **pattern, Atom **witness) {
    Atom *factor = input->input->expr.elems[position + 1u];
    if (!input->explicit_sources) {
        *kind = SOURCE_FACTOR_BTM_V1;
        *pattern = factor;
        *witness = NULL;
        return true;
    }
    const CettaGsltSupportOperatorDeclV1 *declaration =
        find_declaration_v1(
            profile->source_declarations,
            profile->source_declaration_count, factor);
    if (!declaration)
        return false;
    if (strcmp(declaration->operator_id,
               "support.snapshot-match.v1") == 0) {
        *kind = SOURCE_FACTOR_BTM_V1;
        *pattern = factor->expr.elems[1];
        *witness = NULL;
        return true;
    }
    if (strcmp(declaration->operator_id, "support.equal.v1") == 0) {
        *kind = SOURCE_FACTOR_EQUAL_V1;
    } else if (strcmp(declaration->operator_id,
                      "support.not-equal.v1") == 0) {
        *kind = SOURCE_FACTOR_NOT_EQUAL_V1;
    } else {
        return false;
    }
    *pattern = factor->expr.elems[1];
    *witness = factor->expr.elems[2];
    return true;
}

static bool match_input_recurse(
    const CettaGsltSupportTransformProfileV1 *profile,
    SupportSpaceV1 *space, const InputViewV1 *input,
    const size_t *candidates, size_t candidate_count,
    CettaExprIndex position, Bindings *bindings,
    MatchRowsV1 *rows, char *error, size_t error_size) {
    CettaExprLen factor_count = input->input->expr.len - 1u;
    if (position == factor_count)
        return match_rows_append(rows, bindings, error, error_size);

    SourceFactorKindV1 kind;
    Atom *pattern = NULL;
    Atom *witness = NULL;
    if (!factor_view(profile, input, position, &kind, &pattern, &witness))
        return runtime_error(error, error_size,
                             "admitted input factor became malformed");

    Atom *excluded = NULL;
    if (kind != SOURCE_FACTOR_BTM_V1) {
        excluded = bindings_apply(bindings, space->arena, pattern);
        if (!excluded)
            return runtime_error(error, error_size,
                                 "cannot instantiate a source constraint");
    }

    for (size_t candidate_offset = 0u;
         candidate_offset < candidate_count; candidate_offset++) {
        size_t entry_index = candidates[candidate_offset];
        Atom *candidate = space->entries[entry_index].atom;
        if (kind == SOURCE_FACTOR_EQUAL_V1 && !atom_eq(candidate, excluded))
            continue;
        if (kind == SOURCE_FACTOR_NOT_EQUAL_V1 && atom_eq(candidate, excluded))
            continue;

        Bindings next;
        if (!bindings_clone(&next, bindings))
            return runtime_error(error, error_size,
                                 "cannot branch a source match");
        Atom *match_pattern = kind == SOURCE_FACTOR_BTM_V1 ? pattern : witness;
        bool matched = support_match_atom(
            space, match_pattern, candidate, &next);
        if (matched) {
            bool ok = match_input_recurse(
                profile, space, input, candidates, candidate_count,
                position + 1u, &next, rows, error, error_size);
            bindings_free(&next);
            if (!ok)
                return false;
        } else {
            bindings_free(&next);
        }
    }
    return true;
}

static bool collect_matches(
    const CettaGsltSupportTransformProfileV1 *profile,
    SupportSpaceV1 *space, const InputViewV1 *input, MatchRowsV1 *rows,
    char *error, size_t error_size) {
    size_t candidate_count = 0u;
    size_t *candidates = present_indices_sorted(space, &candidate_count);
    Bindings empty;
    bindings_init(&empty);
    bool ok = match_input_recurse(
        profile, space, input, candidates, candidate_count, 0u,
        &empty, rows, error, error_size);
    bindings_free(&empty);
    free(candidates);
    return ok;
}

static bool sink_kind_v1(const char *operator_id, SinkKindV1 *kind) {
    if (strcmp(operator_id, "support.add.v1") == 0)
        *kind = SINK_ADD_V1;
    else if (strcmp(operator_id, "support.remove.v1") == 0)
        *kind = SINK_REMOVE_V1;
    else if (strcmp(operator_id, "support.head.v1") == 0)
        *kind = SINK_HEAD_V1;
    else if (strcmp(operator_id, "support.tail.v1") == 0)
        *kind = SINK_TAIL_V1;
    else if (strcmp(operator_id, "support.group-cardinality.v1") == 0)
        *kind = SINK_GROUP_CARDINALITY_V1;
    else if (strcmp(operator_id,
                    "support.evaluate-project.mm2-pure-f64.v1") == 0)
        *kind = SINK_EVALUATE_PROJECT_PURE_F64_V1;
    else
        return false;
    return true;
}

static void staged_sinks_free_v1(StagedSinkV1 *sinks, size_t sink_count) {
    if (!sinks)
        return;
    for (size_t index = 0u; index < sink_count; index++)
        support_space_free(&sinks[index].staged);
    free(sinks);
}

static bool prepare_staged_sinks_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    SupportSpaceV1 *space, const OutputViewV1 *output,
    StagedSinkV1 **sinks_out, size_t *sink_count_out,
    char *error, size_t error_size) {
    size_t sink_count = (size_t)output->output->expr.len - 1u;
    StagedSinkV1 *sinks = sink_count
        ? calloc(sink_count, sizeof(*sinks)) : NULL;
    if (sink_count && !sinks)
        return runtime_error(error, error_size,
                             "cannot allocate staged output sinks");
    for (size_t index = 0u; index < sink_count; index++) {
        Atom *form = output->output->expr.elems[index + 1u];
        StagedSinkV1 *sink = &sinks[index];
        sink->staged.arena = space->arena;
        if (!output->explicit_sinks) {
            sink->kind = SINK_ADD_V1;
            sink->body = form;
            continue;
        }
        sink->declaration = find_declaration_v1(
            profile->sink_declarations, profile->sink_declaration_count,
            form);
        const CettaGsltSupportOperatorDeclV1 *declaration =
            sink->declaration;
        if (!declaration ||
            !sink_kind_v1(declaration->operator_id, &sink->kind)) {
            staged_sinks_free_v1(sinks, sink_count);
            return runtime_error(error, error_size,
                                 "admitted output sink became malformed");
        }
        if (sink->kind == SINK_HEAD_V1 || sink->kind == SINK_TAIL_V1) {
            if (!positive_u64_atom_v1(form->expr.elems[1], &sink->limit)) {
                staged_sinks_free_v1(sinks, sink_count);
                return runtime_error(error, error_size,
                                     "head/tail requires a positive limit");
            }
            sink->body = form->expr.elems[2];
        } else if (sink->kind == SINK_GROUP_CARDINALITY_V1) {
            sink->body = form->expr.elems[1];
            sink->group_counter = form->expr.elems[2];
            sink->group_witness = form->expr.elems[3];
        } else if (sink->kind == SINK_EVALUATE_PROJECT_PURE_F64_V1) {
            sink->body = form->expr.elems[1];
            sink->evaluation_pattern = form->expr.elems[2];
            sink->evaluation_call = form->expr.elems[3];
        } else {
            sink->body = form->expr.elems[1];
        }
    }
    *sinks_out = sinks;
    *sink_count_out = sink_count;
    return true;
}

static bool stage_outputs_v1(
    SupportSpaceV1 *space, const MatchRowsV1 *rows,
    StagedSinkV1 *sinks, size_t sink_count,
    char *error, size_t error_size) {
    for (size_t row_index = 0u; row_index < rows->len; row_index++) {
        for (size_t sink_index = 0u;
             sink_index < sink_count; sink_index++) {
            StagedSinkV1 *sink = &sinks[sink_index];
            Atom *instantiated = NULL;
            if (sink->kind == SINK_GROUP_CARDINALITY_V1) {
                Atom *parts[3] = {
                    bindings_apply(&rows->items[row_index], space->arena,
                                   sink->body),
                    bindings_apply(&rows->items[row_index], space->arena,
                                   sink->group_counter),
                    bindings_apply(&rows->items[row_index], space->arena,
                                   sink->group_witness),
                };
                if (parts[0] && parts[1] && parts[2])
                    instantiated = atom_expr(space->arena, parts, 3u);
            } else if (sink->kind ==
                       SINK_EVALUATE_PROJECT_PURE_F64_V1) {
                Atom *result_template = bindings_apply(
                    &rows->items[row_index], space->arena, sink->body);
                Atom *pattern = bindings_apply(
                    &rows->items[row_index], space->arena,
                    sink->evaluation_pattern);
                Atom *call = bindings_apply(
                    &rows->items[row_index], space->arena,
                    sink->evaluation_call);
                Atom *value = NULL;
                CettaGsltPureOutcomeV1 outcome = call
                    ? cetta_gslt_pure_f64_evaluate_v1(
                          space->arena, call, &value, error, error_size)
                    : CETTA_GSLT_PURE_FAULT_V1;
                if (outcome == CETTA_GSLT_PURE_FAULT_V1)
                    return runtime_error(
                        error, error_size,
                        "pure-provider evaluation fault");
                if (outcome == CETTA_GSLT_PURE_PRODUCED_V1 &&
                    result_template && pattern && value) {
                    Bindings result_bindings;
                    bindings_init(&result_bindings);
                    if (match_atoms(pattern, value, &result_bindings))
                        instantiated = bindings_apply(
                            &result_bindings, space->arena,
                            result_template);
                    bindings_free(&result_bindings);
                }
                if (outcome == CETTA_GSLT_PURE_DECLINED_V1 ||
                    (outcome == CETTA_GSLT_PURE_PRODUCED_V1 &&
                     !instantiated))
                    continue;
            } else {
                instantiated = bindings_apply(
                    &rows->items[row_index], space->arena, sink->body);
            }
            if (!instantiated)
                return runtime_error(
                    error, error_size,
                    "cannot instantiate a support-transform output");
            if (sink->kind != SINK_GROUP_CARDINALITY_V1 &&
                sink->kind != SINK_EVALUATE_PROJECT_PURE_F64_V1 &&
                atom_has_vars(instantiated))
                continue;
            if (!support_add(&sink->staged, instantiated,
                             error, error_size))
                return false;
        }
    }
    return true;
}

typedef struct {
    Atom *result_template;
    Atom *counter;
    SupportSpaceV1 witnesses;
} CardinalityGroupV1;

static void cardinality_groups_free_v1(
    CardinalityGroupV1 *groups, size_t group_count) {
    if (!groups)
        return;
    for (size_t index = 0u; index < group_count; index++)
        support_space_free(&groups[index].witnesses);
    free(groups);
}

static Atom *cardinality_atom_v1(Arena *arena, size_t count) {
    if (count <= (size_t)INT64_MAX)
        return atom_int(arena, (int64_t)count);
    char text[32];
    int length = snprintf(text, sizeof(text), "%zu", count);
    return length > 0 && (size_t)length < sizeof(text)
        ? atom_bigint(arena, text) : NULL;
}

static bool finalize_group_cardinality_v1(
    SupportSpaceV1 *space, StagedSinkV1 *sink,
    char *error, size_t error_size) {
    CardinalityGroupV1 *groups = NULL;
    size_t group_count = 0u;
    size_t group_cap = 0u;
    for (size_t staged_index = 0u;
         staged_index < sink->staged.len; staged_index++) {
        SupportEntryV1 *entry = &sink->staged.entries[staged_index];
        if (!entry->present)
            continue;
        Atom *row = entry->atom;
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 3u) {
            cardinality_groups_free_v1(groups, group_count);
            return runtime_error(error, error_size,
                                 "group-cardinality staging row is malformed");
        }
        size_t group_index = 0u;
        while (group_index < group_count &&
               !(atom_eq(groups[group_index].result_template,
                         row->expr.elems[0]) &&
                 atom_eq(groups[group_index].counter,
                         row->expr.elems[1])))
            group_index++;
        if (group_index == group_count) {
            if (group_count == group_cap) {
                size_t next = group_cap ? group_cap * 2u : 8u;
                if (next < group_cap ||
                    next > SIZE_MAX / sizeof(*groups)) {
                    cardinality_groups_free_v1(groups, group_count);
                    return runtime_error(
                        error, error_size,
                        "group-cardinality group table is too large");
                }
                groups = cetta_realloc(groups, next * sizeof(*groups));
                group_cap = next;
            }
            CardinalityGroupV1 *group = &groups[group_count++];
            memset(group, 0, sizeof(*group));
            group->result_template = row->expr.elems[0];
            group->counter = row->expr.elems[1];
            group->witnesses.arena = space->arena;
        }
        if (!support_add(&groups[group_index].witnesses,
                         row->expr.elems[2], error, error_size)) {
            cardinality_groups_free_v1(groups, group_count);
            return false;
        }
    }

    for (size_t group_index = 0u;
         group_index < group_count; group_index++) {
        CardinalityGroupV1 *group = &groups[group_index];
        size_t count = 0u;
        for (size_t witness_index = 0u;
             witness_index < group->witnesses.len; witness_index++)
            count += group->witnesses.entries[witness_index].present ? 1u : 0u;
        Atom *output = group->result_template;
        if (group->counter->kind == ATOM_VAR) {
            Atom *count_atom = cardinality_atom_v1(space->arena, count);
            Bindings replacement;
            bindings_init(&replacement);
            bool replaced = count_atom &&
                bindings_add_var_acyclic(
                    &replacement, group->counter, count_atom);
            output = replaced
                ? bindings_apply(&replacement, space->arena,
                                 group->result_template)
                : NULL;
            bindings_free(&replacement);
            if (!output) {
                cardinality_groups_free_v1(groups, group_count);
                return runtime_error(
                    error, error_size,
                    "cannot instantiate group-cardinality result");
            }
        } else {
            uint64_t guard = 0u;
            if (!nonnegative_u64_atom_v1(group->counter, &guard) ||
                guard != (uint64_t)count)
                continue;
        }
        if (!support_add(space, output, error, error_size)) {
            cardinality_groups_free_v1(groups, group_count);
            return false;
        }
    }
    cardinality_groups_free_v1(groups, group_count);
    return true;
}

static bool finalize_staged_sink_v1(
    SupportSpaceV1 *space, StagedSinkV1 *sink,
    char *error, size_t error_size) {
    if (sink->kind == SINK_GROUP_CARDINALITY_V1)
        return finalize_group_cardinality_v1(
            space, sink, error, error_size);
    size_t staged_count = 0u;
    size_t *indices = present_indices_sorted(&sink->staged, &staged_count);
    size_t begin = 0u;
    size_t end = staged_count;
    if (sink->kind == SINK_HEAD_V1 && sink->limit < end)
        end = (size_t)sink->limit;
    if (sink->kind == SINK_TAIL_V1 && sink->limit < end)
        begin = end - (size_t)sink->limit;
    for (size_t index = begin; index < end; index++) {
        Atom *atom = sink->staged.entries[indices[index]].atom;
        if (sink->kind == SINK_REMOVE_V1)
            support_remove(space, atom);
        else if (!support_add(space, atom, error, error_size)) {
            free(indices);
            return false;
        }
    }
    free(indices);
    return true;
}

static bool apply_staged_outputs_v1(
    SupportSpaceV1 *space, StagedSinkV1 *sinks, size_t sink_count,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < sink_count; index++) {
        if (!finalize_staged_sink_v1(
                space, &sinks[index], error, error_size))
            return false;
    }
    return true;
}

static bool execute_one(
    const CettaGsltSupportTransformProfileV1 *profile,
    SupportSpaceV1 *space, bool *moved,
    char *error, size_t error_size) {
    DirectiveViewV1 directive;
    bool found = false;
    if (!select_directive(profile, space, &found, &directive,
                          error, error_size))
        return false;
    if (!found) {
        *moved = false;
        return true;
    }

    MatchRowsV1 rows = {0};
    if (!collect_matches(profile, space, &directive.input, &rows,
                         error, error_size)) {
        match_rows_free(&rows);
        return false;
    }
    StagedSinkV1 *sinks = NULL;
    size_t sink_count = 0u;
    if (!prepare_staged_sinks_v1(
            profile, space, &directive.output, &sinks, &sink_count,
            error, error_size) ||
        !stage_outputs_v1(
            space, &rows, sinks, sink_count, error, error_size)) {
        staged_sinks_free_v1(sinks, sink_count);
        match_rows_free(&rows);
        return false;
    }
    space->entries[directive.entry_index].present = false;
    if (!apply_staged_outputs_v1(
            space, sinks, sink_count, error, error_size)) {
        staged_sinks_free_v1(sinks, sink_count);
        match_rows_free(&rows);
        return false;
    }
    staged_sinks_free_v1(sinks, sink_count);
    match_rows_free(&rows);
    *moved = true;
    return true;
}

static bool support_has_work(
    const CettaGsltSupportTransformProfileV1 *profile,
    SupportSpaceV1 *space, bool *has_work,
    char *error, size_t error_size) {
    DirectiveViewV1 ignored;
    return select_directive(profile, space, has_work, &ignored,
                            error, error_size);
}

bool cetta_gslt_support_transform_run_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    Arena *arena, Atom *const *forms, size_t form_count, uint64_t fuel,
    CettaGsltSupportTransformResultV1 *result,
    char *error, size_t error_size) {
    if (!result)
        return runtime_error(error, error_size,
                             "missing support-transform result");
    memset(result, 0, sizeof(*result));
    if (!arena || (!forms && form_count != 0u))
        return runtime_error(error, error_size,
                             "invalid support-transform inputs");
    if (!cetta_gslt_support_transform_profile_validate_v1(
            profile, error, error_size))
        return false;

    SupportSpaceV1 space = {.arena = arena};
    for (size_t index = 0u; index < form_count; index++) {
        if (!forms[index] ||
            !support_add(&space, forms[index], error, error_size)) {
            support_space_free(&space);
            result->outcome = CETTA_GSLT_SUPPORT_FAULT;
            return false;
        }
    }

    while (result->steps < fuel) {
        bool moved = false;
        if (!execute_one(profile, &space, &moved, error, error_size)) {
            support_space_free(&space);
            result->outcome = CETTA_GSLT_SUPPORT_FAULT;
            return false;
        }
        if (!moved)
            break;
        result->steps++;
    }

    bool has_work = false;
    if (!support_has_work(profile, &space, &has_work, error, error_size)) {
        support_space_free(&space);
        result->outcome = CETTA_GSLT_SUPPORT_FAULT;
        return false;
    }
    result->outcome = has_work
        ? CETTA_GSLT_SUPPORT_EXPIRED : CETTA_GSLT_SUPPORT_COMPLETED;

    size_t final_count = 0u;
    size_t *final_indices = present_indices_sorted(&space, &final_count);
    result->atoms = final_count
        ? cetta_malloc(final_count * sizeof(*result->atoms)) : NULL;
    result->atom_count = final_count;
    for (size_t index = 0u; index < final_count; index++)
        result->atoms[index] = space.entries[final_indices[index]].atom;
    free(final_indices);
    support_space_free(&space);
    return true;
}

void cetta_gslt_support_transform_result_free_v1(
    CettaGsltSupportTransformResultV1 *result) {
    if (!result)
        return;
    free(result->atoms);
    memset(result, 0, sizeof(*result));
}
