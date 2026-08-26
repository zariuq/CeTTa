#include "gdl_positive_horn_native.h"

#include "gdl_source_presentation.h"
#include "native_sha256.h"
#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GDL_POSITIVE_HORN_DEFAULT_MAX_BLOCKS_V1 = 65536u,
    GDL_POSITIVE_HORN_DEFAULT_MAX_EPISODE_FACTS_V1 = 65536u,
    GDL_POSITIVE_HORN_DEFAULT_MAX_EPISODE_BLOCKS_V1 = 65536u,
};

typedef struct {
    const char *name;
    Atom *atom;
    bool bound;
} GdlPositiveHornVariableV1;

typedef struct {
    GdlPositiveHornVariableV1 *items;
    size_t count;
    size_t capacity;
} GdlPositiveHornVariablesV1;

typedef struct {
    Atom **items;
    size_t count;
    size_t capacity;
} GdlPositiveHornAtomsV1;

typedef struct {
    Arena *arena;
    GdlPositiveHornVariablesV1 variables;
} GdlPositiveHornBlockContextV1;

typedef enum {
    GDL_POSITIVE_HORN_SOURCE_ONLY_V1 = 0,
    GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1,
} GdlPositiveHornSourceModeV1;

typedef struct {
    Atom *source;
    Atom *authored_member;
    Atom *authored_literal;
    Atom *structural_literal;
    Atom *absence_goal;
} GdlPositiveHornFiniteDomainMemberV1;

typedef struct {
    GdlPositiveHornFiniteDomainMemberV1 *items;
    size_t count;
    size_t capacity;
} GdlPositiveHornFiniteDomainV1;

struct CettaGdlPositiveHornNativeV1 {
    Arena arena;
    CettaGdlTypeOfNativeV1 *typing;
    GdlSourcePackageV1 package;
    CettaNikDirectAuthorityTokenV1 token;
    const CettaNikDirectAuthorityV1 *authority;
    GdlPositiveHornSourceModeV1 mode;
    GdlPositiveHornFiniteDomainV1 finite_domain;
    CettaGdlStratificationV1 *stratification;
    CettaGdlPositiveHornLimitsV1 limits;
    CettaGdlPositiveHornStatsV1 stats;
    Atom *base_artifact;
};

struct CettaGdlPositiveHornEpisodeV1 {
    Arena arena;
    /* Borrowed.  The immutable source-native calculus owns the base
     * artifact and must outlive this finite extension. */
    CettaGdlPositiveHornNativeV1 *native;
    CettaNikDirectAuthorityTokenV1 source_token;
    CettaNikDirectAuthorityTokenV1 token;
    char digest[65];
    char *revision;
    Atom *identity;
    Atom *artifact;
    Space *complete_space;
    SpaceReadToken complete_read;
    bool has_complete_space_read;
    CettaGdlPositiveHornEpisodeStatsV1 stats;
};

static const CettaNikDirectAuthorityV1
    g_gdl_positive_horn_authority_v1 = {
        .alias = "gdl-positive-horn-native-v1",
        .system_id = "gdl.positive-horn.native.v1",
        .authority_identity = UINT64_C(0x67646c2e686f726e),
        .realization_identity = UINT64_C(0x67646c2e70686e31),
        .authority_revision = 1u,
        .realization_abi = 1u,
    };

static const CettaNikDirectAuthorityV1
    g_gdl_finite_view_authority_v1 = {
        .alias = "gdl-finite-view-native-v1",
        .system_id = "gdl.finite-view.native.v1",
        .authority_identity = UINT64_C(0x67646c2e66696e76),
        .realization_identity = UINT64_C(0x67646c2e666e7631),
        .authority_revision = 1u,
        .realization_abi = 1u,
    };

typedef enum {
    GDL_POSITIVE_HORN_BUILD_OK_V1 = 0,
    GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1,
    GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1,
    GDL_POSITIVE_HORN_BUILD_FAULT_V1,
} GdlPositiveHornBuildV1;

static bool gdl_positive_horn_reserve_v1(
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

static bool gdl_positive_horn_atoms_push_v1(
    GdlPositiveHornAtomsV1 *atoms, Atom *atom) {
    if (!atoms || !atom || !gdl_positive_horn_reserve_v1(
            (void **)&atoms->items, &atoms->capacity, atoms->count + 1u,
            sizeof(*atoms->items)))
        return false;
    atoms->items[atoms->count++] = atom;
    return true;
}

static bool gdl_positive_horn_finite_domain_push_v1(
    GdlPositiveHornFiniteDomainV1 *domain,
    GdlPositiveHornFiniteDomainMemberV1 member) {
    if (!domain || !member.source || !member.authored_member ||
        !member.authored_literal || !member.structural_literal ||
        !member.absence_goal ||
        !gdl_positive_horn_reserve_v1(
            (void **)&domain->items, &domain->capacity,
            domain->count + 1u, sizeof(*domain->items)))
        return false;
    domain->items[domain->count++] = member;
    return true;
}

static Atom *gdl_positive_horn_expr_v1(
    Arena *arena, const char *head, Atom *const *arguments,
    size_t argument_count) {
    if (!arena || !head || !*head ||
        argument_count > (size_t)UINT32_MAX - 1u)
        return NULL;
    Atom **items = arena_alloc(
        arena, (argument_count + 1u) * sizeof(*items));
    items[0] = atom_symbol(arena, head);
    for (size_t index = 0u; index < argument_count; index++)
        items[index + 1u] = arguments[index];
    return atom_expr(arena, items, (CettaExprLen)(argument_count + 1u));
}

static Atom *gdl_positive_horn_quote_v1(Arena *arena, Atom *value) {
    Atom *arguments[] = {value};
    return value
        ? gdl_positive_horn_expr_v1(arena, "quote", arguments, 1u)
        : NULL;
}

static Atom *gdl_positive_horn_token_v1(Arena *arena, const char *token) {
    char *end = NULL;
    intmax_t value;
    if (!arena || !token || !*token)
        return NULL;
    errno = 0;
    value = strtoimax(token, &end, 10);
    if (errno == 0 && end && *end == '\0' && value >= INT64_MIN &&
        value <= INT64_MAX)
        return atom_int(arena, (int64_t)value);
    return atom_symbol(arena, token);
}

static Atom *gdl_positive_horn_variable_v1(
    GdlPositiveHornBlockContextV1 *context, const char *name) {
    if (!context || !name || name[0] != '?' || name[1] == '\0')
        return NULL;
    for (size_t index = 0u; index < context->variables.count; index++)
        if (strcmp(context->variables.items[index].name, name) == 0)
            return context->variables.items[index].atom;
    if (!gdl_positive_horn_reserve_v1(
            (void **)&context->variables.items,
            &context->variables.capacity,
            context->variables.count + 1u,
            sizeof(*context->variables.items)))
        return NULL;
    GdlPositiveHornVariableV1 *binding =
        &context->variables.items[context->variables.count++];
    binding->name = name;
    binding->atom = atom_var_with_id(
        context->arena, name + 1u, fresh_var_id());
    binding->bound = false;
    return binding->atom;
}

static bool gdl_positive_horn_variables_visit_v1(
    GdlPositiveHornBlockContextV1 *context,
    const GdlSourceRawExprV1 *raw,
    bool require_bound,
    bool mark_bound) {
    if (!context || !raw)
        return false;
    if (raw->token) {
        if (raw->token[0] != '?')
            return true;
        Atom *ignored = gdl_positive_horn_variable_v1(context, raw->token);
        if (!ignored)
            return false;
        for (size_t index = 0u; index < context->variables.count; index++) {
            GdlPositiveHornVariableV1 *binding =
                &context->variables.items[index];
            if (strcmp(binding->name, raw->token) != 0)
                continue;
            if (require_bound && !binding->bound)
                return false;
            if (mark_bound)
                binding->bound = true;
            return true;
        }
        return false;
    }
    for (size_t index = 0u; index < raw->count; index++)
        if (!gdl_positive_horn_variables_visit_v1(
                context, raw->items[index], require_bound, mark_bound))
            return false;
    return true;
}

static Atom *gdl_positive_horn_authored_v1(
    GdlPositiveHornBlockContextV1 *context,
    const GdlSourceRawExprV1 *raw,
    size_t depth) {
    if (!context || !raw || depth > 4096u)
        return NULL;
    if (raw->token)
        return raw->token[0] == '?'
            ? gdl_positive_horn_variable_v1(context, raw->token)
            : gdl_positive_horn_token_v1(context->arena, raw->token);
    if (raw->count == 0u || !raw->items[0]->token ||
        raw->items[0]->token[0] == '?')
        return NULL;
    Atom **items = arena_alloc(
        context->arena, raw->count * sizeof(*items));
    items[0] = atom_symbol(context->arena, raw->items[0]->token);
    for (size_t index = 1u; index < raw->count; index++) {
        items[index] = gdl_positive_horn_authored_v1(
            context, raw->items[index], depth + 1u);
        if (!items[index])
            return NULL;
    }
    return atom_expr(context->arena, items, (CettaExprLen)raw->count);
}

static bool gdl_positive_horn_raw_has_variables_v1(
    const GdlSourceRawExprV1 *raw, size_t depth) {
    if (!raw || depth > 4096u)
        return true;
    if (raw->token)
        return raw->token[0] == '?';
    for (size_t index = 0u; index < raw->count; index++)
        if (gdl_positive_horn_raw_has_variables_v1(
                raw->items[index], depth + 1u))
            return true;
    return false;
}

static bool gdl_positive_horn_relation_signature_v1(
    Atom *relation, const char **head_out, size_t *arity_out) {
    if (!relation || !head_out || !arity_out)
        return false;
    if (relation->kind == ATOM_SYMBOL) {
        *head_out = atom_name_cstr(relation);
        *arity_out = 0u;
        return *head_out != NULL;
    }
    if (relation->kind != ATOM_EXPR || relation->expr.len == 0u ||
        relation->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    *head_out = atom_name_cstr(relation->expr.elems[0]);
    *arity_out = (size_t)relation->expr.len - 1u;
    return *head_out != NULL;
}

static Atom *gdl_positive_horn_absence_goal_v1(
    Arena *arena, Atom *structural_literal) {
    static const char prefix[] = "gdl:finite-relation-absent-v1:";
    const char *head = NULL;
    size_t arity = 0u;
    if (!arena || !gdl_positive_horn_relation_signature_v1(
            structural_literal, &head, &arity))
        return NULL;
    size_t head_length = strlen(head);
    if (head_length > SIZE_MAX - sizeof(prefix))
        return NULL;
    char *absence_head = arena_alloc(
        arena, sizeof(prefix) + head_length);
    memcpy(absence_head, prefix, sizeof(prefix) - 1u);
    memcpy(
        absence_head + sizeof(prefix) - 1u,
        head, head_length + 1u);
    if (arity == 0u)
        return atom_symbol(arena, absence_head);
    Atom **arguments = arena_alloc(arena, arity * sizeof(*arguments));
    for (size_t index = 0u; index < arity; index++)
        arguments[index] = structural_literal->expr.elems[index + 1u];
    return gdl_positive_horn_expr_v1(
        arena, absence_head, arguments, arity);
}

static bool gdl_positive_horn_finite_domain_supports_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    Atom *structural_literal) {
    const char *head = NULL;
    size_t arity = 0u;
    if (!native || !structural_literal ||
        !gdl_positive_horn_relation_signature_v1(
            structural_literal, &head, &arity))
        return false;
    bool ground = !atom_has_vars(structural_literal);
    for (size_t index = 0u;
         index < native->finite_domain.count; index++) {
        Atom *candidate =
            native->finite_domain.items[index].structural_literal;
        const char *candidate_head = NULL;
        size_t candidate_arity = 0u;
        if (!gdl_positive_horn_relation_signature_v1(
                candidate, &candidate_head, &candidate_arity))
            return false;
        if (strcmp(head, candidate_head) != 0 || arity != candidate_arity)
            continue;
        if (!ground || atom_eq(structural_literal, candidate))
            return true;
    }
    return false;
}

static void gdl_positive_horn_sha_length_v1(
    CettaNativeSha256 *sha, size_t length) {
    uint8_t bytes[8];
    uint64_t value = (uint64_t)length;
    for (size_t index = 0u; index < sizeof(bytes); index++)
        bytes[sizeof(bytes) - 1u - index] =
            (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static bool gdl_positive_horn_episode_digest_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    Arena *scratch,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    char digest_out[65]) {
    static const char positive_domain[] =
        "cetta.gdl-positive-horn.typed-episode.v1";
    static const char finite_domain[] =
        "cetta.gdl-finite-view.typed-episode.v1";
    if (!native || !scratch || !episode_identity || !digest_out ||
        (fact_count != 0u && !facts))
        return false;
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    const char *domain = native->mode ==
            GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1
        ? finite_domain : positive_domain;
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, strlen(domain) + 1u);
    size_t source_length = strlen(native->package.calculus_input_sha256);
    gdl_positive_horn_sha_length_v1(&sha, source_length);
    cetta_native_sha256_update(
        &sha,
        (const uint8_t *)native->package.calculus_input_sha256,
        source_length);
    char *identity_text = atom_to_parseable_string(
        scratch, episode_identity);
    if (!identity_text || !*identity_text)
        return false;
    size_t identity_length = strlen(identity_text);
    gdl_positive_horn_sha_length_v1(&sha, identity_length);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)identity_text, identity_length);
    gdl_positive_horn_sha_length_v1(&sha, fact_count);
    for (size_t index = 0u; index < fact_count; index++) {
        char *fact_text = facts[index]
            ? atom_to_parseable_string(scratch, facts[index])
            : NULL;
        if (!fact_text || !*fact_text)
            return false;
        size_t fact_length = strlen(fact_text);
        gdl_positive_horn_sha_length_v1(&sha, fact_length);
        cetta_native_sha256_update(
            &sha, (const uint8_t *)fact_text, fact_length);
    }
    cetta_native_sha256_finish_hex(&sha, digest_out);
    return true;
}

static Atom *gdl_positive_horn_source_receipt_v1(
    Arena *arena, size_t ordinal, const GdlSourceRawFormV1 *form) {
    Atom *arguments[] = {
        atom_int(arena, (int64_t)(ordinal + 1u)),
        atom_int(arena, (int64_t)form->start_line),
        atom_int(arena, (int64_t)form->end_line),
    };
    return gdl_positive_horn_expr_v1(
        arena, "gdl:source-occurrence", arguments, 3u);
}

static Atom *gdl_positive_horn_premise_list_v1(
    Arena *arena, Atom *const *proofs, Atom *const *goals, size_t count) {
    Atom *result = atom_symbol(arena, "rm-nil");
    while (result && count > 0u) {
        count--;
        Atom *premise_arguments[] = {
            proofs[count], gdl_positive_horn_quote_v1(arena, goals[count]),
        };
        Atom *premise = gdl_positive_horn_expr_v1(
            arena, "rm-premise", premise_arguments, 2u);
        Atom *cons_arguments[] = {premise, result};
        result = premise && premise_arguments[1]
            ? gdl_positive_horn_expr_v1(
                arena, "rm-cons", cons_arguments, 2u)
            : NULL;
    }
    return result;
}

static Atom *gdl_positive_horn_block_v1(
    Arena *arena,
    Atom *id,
    Atom *source,
    Atom *proof,
    Atom *premises,
    Atom *conclusion) {
    Atom *arguments[] = {
        id, source, gdl_positive_horn_quote_v1(arena, proof), premises,
        gdl_positive_horn_quote_v1(arena, conclusion),
    };
    if (!arguments[0] || !arguments[1] || !arguments[2] || !arguments[3] ||
        !arguments[4])
        return NULL;
    return gdl_positive_horn_expr_v1(arena, "rm-block", arguments, 5u);
}

static bool gdl_positive_horn_raw_head_v1(
    const GdlSourceRawExprV1 *raw, const char *head) {
    return raw && !raw->token && raw->count > 0u && raw->items[0]->token &&
        strcmp(raw->items[0]->token, head) == 0;
}

static GdlPositiveHornBuildV1
gdl_positive_horn_collect_finite_domain_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const GdlSourceRawFormsV1 *forms) {
    if (!native || !forms)
        return GDL_POSITIVE_HORN_BUILD_FAULT_V1;
    for (size_t ordinal = 0u; ordinal < forms->count; ordinal++) {
        const GdlSourceRawExprV1 *form = forms->items[ordinal].form;
        if (!form)
            return GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        if (gdl_positive_horn_raw_head_v1(form, "<=")) {
            if (form->count > 1u &&
                gdl_positive_horn_raw_head_v1(form->items[1], "base"))
                return GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
            continue;
        }
        if (!gdl_positive_horn_raw_head_v1(form, "base"))
            continue;
        if (form->count != 2u ||
            gdl_positive_horn_raw_has_variables_v1(
                form->items[1], 0u))
            return GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;

        GdlPositiveHornBlockContextV1 context = {
            .arena = &native->arena,
        };
        Atom *member = gdl_positive_horn_authored_v1(
            &context, form->items[1], 0u);
        Atom *literal_arguments[] = {member};
        Atom *authored_literal = member
            ? gdl_positive_horn_expr_v1(
                &native->arena, "true", literal_arguments, 1u)
            : NULL;
        Atom *structural_literal = authored_literal;
        Atom *source = gdl_positive_horn_source_receipt_v1(
            &native->arena, ordinal, &forms->items[ordinal]);
        Atom *absence_goal = gdl_positive_horn_absence_goal_v1(
            &native->arena, structural_literal);
        bool pushed = gdl_positive_horn_finite_domain_push_v1(
            &native->finite_domain,
            (GdlPositiveHornFiniteDomainMemberV1){
                .source = source,
                .authored_member = member,
                .authored_literal = authored_literal,
                .structural_literal = structural_literal,
                .absence_goal = absence_goal,
            });
        free(context.variables.items);
        if (!pushed)
            return GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
        native->stats.finite_state_domain_members++;
    }
    return native->finite_domain.count != 0u
        ? GDL_POSITIVE_HORN_BUILD_OK_V1
        : GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
}

static GdlPositiveHornBuildV1 gdl_positive_horn_source_block_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const GdlSourceRawFormV1 *source_form,
    size_t ordinal,
    GdlPositiveHornAtomsV1 *blocks,
    GdlPositiveHornAtomsV1 *distinct_types) {
    const GdlSourceRawExprV1 *form = source_form->form;
    bool rule = gdl_positive_horn_raw_head_v1(form, "<=");
    size_t conclusion_index = rule ? 1u : 0u;
    size_t premise_start = rule ? 2u : form->count;
    if (!form || (rule && form->count < 2u))
        return GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
    const GdlSourceRawExprV1 *conclusion = rule
        ? form->items[conclusion_index]
        : form;
    GdlPositiveHornBlockContextV1 context = {.arena = &native->arena};
    Atom *structural = gdl_positive_horn_authored_v1(
        &context, conclusion, 0u);
    size_t premise_count = rule ? form->count - premise_start : 0u;
    Atom **proof_variables = premise_count
        ? cetta_malloc(premise_count * sizeof(*proof_variables))
        : NULL;
    Atom **goals = premise_count
        ? cetta_malloc(premise_count * sizeof(*goals))
        : NULL;
    GdlPositiveHornBuildV1 status = GDL_POSITIVE_HORN_BUILD_OK_V1;
    if (!structural ||
        (premise_count && (!proof_variables || !goals))) {
        status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
        goto done;
    }

    for (size_t index = 0u; index < premise_count; index++) {
        const GdlSourceRawExprV1 *premise = form->items[premise_start + index];
        Atom *finite_absence_goal = NULL;
        if (gdl_positive_horn_raw_head_v1(premise, "or")) {
            status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
            goto done;
        }
        if (gdl_positive_horn_raw_head_v1(premise, "not")) {
            const GdlSourceRawExprV1 *operand = premise->count == 2u
                ? premise->items[1] : NULL;
            if (native->mode != GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1 ||
                !operand || operand->count != 2u ||
                !gdl_positive_horn_raw_head_v1(operand, "true") ||
                !gdl_positive_horn_variables_visit_v1(
                    &context, operand, true, false)) {
                status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
                goto done;
            }
            Atom *structural_absent_literal =
                gdl_positive_horn_authored_v1(&context, operand, 0u);
            if (!structural_absent_literal ||
                !gdl_positive_horn_finite_domain_supports_v1(
                    native, structural_absent_literal)) {
                status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
                goto done;
            }
            finite_absence_goal = gdl_positive_horn_absence_goal_v1(
                &native->arena, structural_absent_literal);
            if (!finite_absence_goal) {
                status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
                goto done;
            }
            native->stats.finite_state_negative_premises++;
        } else if (gdl_positive_horn_raw_head_v1(premise, "distinct")) {
            if (premise->count != 3u ||
                !gdl_positive_horn_variables_visit_v1(
                    &context, premise, true, false)) {
                status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
                goto done;
            }
            char path[96];
            for (size_t operand = 0u; operand < 2u; operand++) {
                const char *type_name = NULL;
                int written = snprintf(
                    path, sizeof(path), "%zu.%zu",
                    premise_start + index, operand + 1u);
                if (written < 0 || (size_t)written >= sizeof(path) ||
                    !cetta_gdl_type_of_native_source_type_name_v1(
                        native->typing, ordinal, path, &type_name)) {
                    status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
                    goto done;
                }
                bool seen = false;
                for (size_t seen_index = 0u;
                     seen_index < distinct_types->count; seen_index++)
                    if (strcmp(
                            atom_name_cstr(distinct_types->items[seen_index]),
                            type_name) == 0)
                        seen = true;
                if (!seen && !gdl_positive_horn_atoms_push_v1(
                        distinct_types,
                        atom_symbol(&native->arena, type_name))) {
                    status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
                    goto done;
                }
            }
            native->stats.distinct_premises++;
        } else if (!gdl_positive_horn_variables_visit_v1(
                       &context, premise, false, true)) {
            status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
            goto done;
        }
        proof_variables[index] = atom_var_with_id(
            &native->arena, "gdl-proof", fresh_var_id());
        goals[index] = finite_absence_goal
            ? finite_absence_goal
            : gdl_positive_horn_authored_v1(&context, premise, 0u);
        if (!proof_variables[index] || !goals[index]) {
            status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
            goto done;
        }
    }
    if (!gdl_positive_horn_variables_visit_v1(
            &context, conclusion, true, false)) {
        status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        goto done;
    }

    Atom *source = gdl_positive_horn_source_receipt_v1(
        &native->arena, ordinal, source_form);
    Atom *judgment_arguments[] = {structural};
    Atom *proof_premises = gdl_positive_horn_expr_v1(
        &native->arena, "gdl:premises", proof_variables, premise_count);
    Atom *proof_arguments[] = {
        source,
        gdl_positive_horn_expr_v1(
            &native->arena, "gdl:structural-judgment",
            judgment_arguments, 1u),
        proof_premises,
    };
    char block_id_text[64];
    int block_id_length = snprintf(
        block_id_text, sizeof(block_id_text), "gdl-source-%04zu", ordinal + 1u);
    Atom *premises = gdl_positive_horn_premise_list_v1(
        &native->arena, proof_variables, goals, premise_count);
    Atom *proof = gdl_positive_horn_expr_v1(
        &native->arena, rule ? "gdl:rule" : "gdl:fact",
        proof_arguments, 3u);
    Atom *block = block_id_length > 0 &&
            (size_t)block_id_length < sizeof(block_id_text)
        ? gdl_positive_horn_block_v1(
            &native->arena, atom_symbol(&native->arena, block_id_text),
            source, proof, premises, structural)
        : NULL;
    if (!source || !proof_arguments[1] || !proof_premises || !premises ||
        !proof || !block ||
        !gdl_positive_horn_atoms_push_v1(blocks, block)) {
        status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
        goto done;
    }
    if (rule)
        native->stats.source_rules++;
    else
        native->stats.source_facts++;

done:
    free(context.variables.items);
    free(proof_variables);
    free(goals);
    return status;
}

static bool gdl_positive_horn_type_accepts_v1(
    const GdlSourceProfileV1 *profile,
    const char *actual,
    const char *expected,
    size_t depth) {
    if (!profile || !actual || !expected || depth > profile->subtype_count)
        return false;
    if (strcmp(actual, expected) == 0)
        return true;
    for (size_t index = 0u; index < profile->subtype_count; index++)
        if (strcmp(profile->subtypes[index].subtype, actual) == 0 &&
            gdl_positive_horn_type_accepts_v1(
                profile, profile->subtypes[index].supertype,
                expected, depth + 1u))
            return true;
    return false;
}

static GdlPositiveHornBuildV1 gdl_positive_horn_distinct_blocks_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const GdlSourceProfileV1 *profile,
    const GdlPositiveHornAtomsV1 *types,
    GdlPositiveHornAtomsV1 *blocks) {
    GdlPositiveHornAtomsV1 domain = {0};
    GdlPositiveHornBuildV1 status = GDL_POSITIVE_HORN_BUILD_OK_V1;
    if (types->count == 0u)
        return status;
    for (size_t index = 0u; index < profile->signature_count; index++) {
        const GdlSourceSignatureV1 *signature = &profile->signatures[index];
        bool accepted = false;
        for (size_t type_index = 0u; type_index < types->count; type_index++)
            if (gdl_positive_horn_type_accepts_v1(
                    profile, signature->result_type,
                    atom_name_cstr(types->items[type_index]), 0u))
                accepted = true;
        if (!accepted)
            continue;
        if (signature->argument_count != 0u) {
            status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
            goto done;
        }
        Atom *candidate = gdl_positive_horn_token_v1(
            &native->arena, signature->name);
        if (!candidate) {
            status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
            goto done;
        }
        bool seen = false;
        for (size_t term_index = 0u; term_index < domain.count; term_index++)
            if (atom_eq(domain.items[term_index], candidate))
                seen = true;
        if (!seen && !gdl_positive_horn_atoms_push_v1(
                &domain, candidate)) {
            status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
            goto done;
        }
    }
    if (domain.count == 0u) {
        status = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        goto done;
    }
    for (size_t left = 0u; left < domain.count; left++) {
        for (size_t right = 0u; right < domain.count; right++) {
            if (atom_eq(domain.items[left], domain.items[right]))
                continue;
            size_t ordinal = native->stats.distinct_evidence_blocks + 1u;
            Atom *receipt_arguments[] = {
                atom_symbol(&native->arena, native->package.source_sha256),
                atom_symbol(&native->arena, native->package.profile_sha256),
                atom_int(&native->arena, (int64_t)ordinal),
            };
            Atom *source = gdl_positive_horn_expr_v1(
                &native->arena, "gdl:distinct-domain",
                receipt_arguments, 3u);
            Atom *conclusion_arguments[] = {
                domain.items[left], domain.items[right],
            };
            Atom *conclusion = gdl_positive_horn_expr_v1(
                &native->arena, "distinct", conclusion_arguments, 2u);
            Atom *proof_arguments[] = {
                source, domain.items[left], domain.items[right],
            };
            Atom *proof = gdl_positive_horn_expr_v1(
                &native->arena, "gdl:distinct-proof",
                proof_arguments, 3u);
            char id_text[64];
            int id_length = snprintf(
                id_text, sizeof(id_text), "gdl-distinct-%04zu", ordinal);
            Atom *block = id_length > 0 &&
                    (size_t)id_length < sizeof(id_text)
                ? gdl_positive_horn_block_v1(
                    &native->arena, atom_symbol(&native->arena, id_text),
                    source, proof, atom_symbol(&native->arena, "rm-nil"),
                    conclusion)
                : NULL;
            if (!source || !conclusion || !proof || !block ||
                !gdl_positive_horn_atoms_push_v1(blocks, block)) {
                status = GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1;
                goto done;
            }
            native->stats.distinct_evidence_blocks++;
        }
    }
done:
    free(domain.items);
    return status;
}

static bool gdl_positive_horn_error_v1(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom_is_symbol(atom->expr.elems[0], "Error");
}

static bool gdl_positive_horn_build_token_v1(
    CettaGdlPositiveHornNativeV1 *native) {
    if (!native)
        return false;
    return cetta_nik_direct_authority_v1_token_from_sha256(
        native->authority, native->package.calculus_input_sha256,
        1u, &native->token);
}

static CettaGdlPositiveHornAdmissionV1
gdl_positive_horn_admission_v1(
    CettaGdlPositiveHornAdmissionKindV1 kind,
    CettaGdlPositiveHornNativeV1 *native) {
    return (CettaGdlPositiveHornAdmissionV1){
        .kind = kind,
        .native = native,
    };
}

static CettaGdlPositiveHornAdmissionV1
gdl_positive_horn_native_admit_mode_v1(
    Atom *source_program,
    CettaGdlPositiveHornLimitsV1 limits,
    GdlPositiveHornSourceModeV1 mode) {
    GdlSourcePackageV1 package = {0};
    GdlSourceParseV1 package_status = gdl_source_package_view_v1(
        source_program, NULL, NULL, NULL, &package);
    if (package_status != GDL_SOURCE_PARSE_OK_V1) {
        CettaGdlPositiveHornAdmissionKindV1 kind =
            package_status == GDL_SOURCE_PARSE_ENGINE_FAULT_V1
                ? CETTA_GDL_POSITIVE_HORN_ENGINE_FAULT_V1
                : package_status == GDL_SOURCE_PARSE_INCOMPLETE_V1
                    ? CETTA_GDL_POSITIVE_HORN_INCOMPLETE_V1
                    : CETTA_GDL_POSITIVE_HORN_OUTSIDE_FRAGMENT_V1;
        return gdl_positive_horn_admission_v1(kind, NULL);
    }
    CettaGdlTypeOfNativeAdmissionV1 typing =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source_program, limits.typing);
    if (typing.kind != CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 ||
        !typing.native) {
        CettaGdlPositiveHornAdmissionKindV1 kind =
            typing.kind == CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1
                ? CETTA_GDL_POSITIVE_HORN_ENGINE_FAULT_V1
                : typing.kind == CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1
                    ? CETTA_GDL_POSITIVE_HORN_INCOMPLETE_V1
                    : CETTA_GDL_POSITIVE_HORN_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_type_of_native_destroy_v1(typing.native);
        return gdl_positive_horn_admission_v1(kind, NULL);
    }

    CettaGdlPositiveHornNativeV1 *native = cetta_malloc(sizeof(*native));
    memset(native, 0, sizeof(*native));
    arena_init(&native->arena);
    arena_set_runtime_kind(
        &native->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    native->typing = typing.native;
    native->mode = mode;
    native->authority = mode == GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1
        ? &g_gdl_finite_view_authority_v1
        : &g_gdl_positive_horn_authority_v1;
    native->package = package;
    native->package.source_text = arena_strdup(
        &native->arena, package.source_text);
    native->package.profile_text = arena_strdup(
        &native->arena, package.profile_text);
    if (!native->package.source_text || !native->package.profile_text) {
        cetta_gdl_positive_horn_native_destroy_v1(native);
        return gdl_positive_horn_admission_v1(
            CETTA_GDL_POSITIVE_HORN_ENGINE_FAULT_V1, NULL);
    }
    native->limits = limits;
    if (native->limits.max_source_blocks == 0u)
        native->limits.max_source_blocks =
            GDL_POSITIVE_HORN_DEFAULT_MAX_BLOCKS_V1;

    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    GdlSourceRawFormsV1 forms = {0};
    GdlSourceProfileV1 profile = {0};
    GdlPositiveHornAtomsV1 blocks = {0};
    GdlPositiveHornAtomsV1 distinct_types = {0};
    GdlPositiveHornBuildV1 built = GDL_POSITIVE_HORN_BUILD_OK_V1;
    GdlSourceParseV1 parsed = gdl_source_parse_forms_v1(
        &scratch, package.source_text,
        limits.typing.max_derivation_depth
            ? limits.typing.max_derivation_depth : 4096u,
        &forms);
    if (parsed != GDL_SOURCE_PARSE_OK_V1 || forms.foreign_lines != 0u) {
        built = parsed == GDL_SOURCE_PARSE_INCOMPLETE_V1
            ? GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1
            : GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        goto done;
    }
    CettaGdlStratificationResultV1 stratification =
        cetta_gdl_stratification_construct_v1(
            &forms,
            (CettaGdlStratificationLimitsV1){
                .max_logical_depth =
                    limits.typing.max_derivation_depth,
            });
    if (stratification.kind !=
            CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 ||
        !stratification.analysis) {
        built = stratification.kind ==
                CETTA_GDL_STRATIFICATION_INCOMPLETE_V1
            ? GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1
            : stratification.kind ==
                    CETTA_GDL_STRATIFICATION_ENGINE_FAULT_V1
                ? GDL_POSITIVE_HORN_BUILD_FAULT_V1
                : GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        cetta_gdl_stratification_destroy_v1(stratification.analysis);
        goto done;
    }
    native->stratification = stratification.analysis;
    native->stats.dependency_relations =
        cetta_gdl_stratification_relation_count_v1(
            native->stratification);
    native->stats.dependency_edges =
        cetta_gdl_stratification_edge_count_v1(native->stratification);
    native->stats.dependency_strata =
        cetta_gdl_stratification_maximum_stratum_v1(
            native->stratification) + 1u;
    for (size_t edge_index = 0u;
         edge_index < native->stats.dependency_edges; edge_index++) {
        CettaGdlDependencyEdgeViewV1 edge = {0};
        if (!cetta_gdl_stratification_edge_view_v1(
                native->stratification, edge_index, &edge)) {
            built = GDL_POSITIVE_HORN_BUILD_FAULT_V1;
            goto done;
        }
        if (edge.negative)
            native->stats.dependency_negative_edges++;
    }
    parsed = gdl_source_parse_profile_v1(
        &scratch, package.profile_text, &profile);
    if (parsed != GDL_SOURCE_PARSE_OK_V1) {
        built = parsed == GDL_SOURCE_PARSE_INCOMPLETE_V1
            ? GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1
            : GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        goto done;
    }
    if (native->mode == GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1) {
        built = gdl_positive_horn_collect_finite_domain_v1(
            native, &forms);
        if (built != GDL_POSITIVE_HORN_BUILD_OK_V1)
            goto done;
    }
    native->stats.source_forms = forms.count;
    for (size_t ordinal = 0u; ordinal < forms.count; ordinal++) {
        built = gdl_positive_horn_source_block_v1(
            native, &forms.items[ordinal], ordinal,
            &blocks, &distinct_types);
        if (built != GDL_POSITIVE_HORN_BUILD_OK_V1)
            goto done;
    }
    if (native->mode == GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1 &&
        native->stats.finite_state_negative_premises == 0u) {
        built = GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        goto done;
    }
    built = gdl_positive_horn_distinct_blocks_v1(
        native, &profile, &distinct_types, &blocks);
    if (built != GDL_POSITIVE_HORN_BUILD_OK_V1)
        goto done;
    if (blocks.count == 0u ||
        blocks.count > native->limits.max_source_blocks ||
        blocks.count > (size_t)UINT32_MAX) {
        built = blocks.count > native->limits.max_source_blocks
            ? GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1
            : GDL_POSITIVE_HORN_BUILD_OUTSIDE_V1;
        goto done;
    }
    Atom **package_items = arena_alloc(
        &native->arena, (blocks.count + 1u) * sizeof(*package_items));
    package_items[0] = atom_symbol(&native->arena, "rm-package");
    for (size_t index = 0u; index < blocks.count; index++)
        package_items[index + 1u] = blocks.items[index];
    Atom *rule_package = atom_expr(
        &native->arena, package_items, (CettaExprLen)(blocks.count + 1u));
    Atom *revision = atom_symbol(&native->arena, package.revision);
    Atom *head = atom_symbol(&native->arena, "compile:rule-package");
    Atom *arguments[] = {revision, rule_package};
    native->base_artifact = cetta_rule_machine_dispatch(
        &native->arena, head, arguments, 2u);
    if (!native->base_artifact ||
        gdl_positive_horn_error_v1(native->base_artifact) ||
        !gdl_positive_horn_build_token_v1(native)) {
        built = GDL_POSITIVE_HORN_BUILD_FAULT_V1;
        goto done;
    }
    native->stats.compiled_blocks = blocks.count;

done:
    gdl_source_raw_forms_free_v1(&forms);
    gdl_source_profile_free_v1(&profile);
    free(blocks.items);
    free(distinct_types.items);
    arena_free(&scratch);
    if (built != GDL_POSITIVE_HORN_BUILD_OK_V1) {
        CettaGdlPositiveHornAdmissionKindV1 kind =
            built == GDL_POSITIVE_HORN_BUILD_INCOMPLETE_V1
                ? CETTA_GDL_POSITIVE_HORN_INCOMPLETE_V1
                : built == GDL_POSITIVE_HORN_BUILD_FAULT_V1
                    ? CETTA_GDL_POSITIVE_HORN_ENGINE_FAULT_V1
                    : CETTA_GDL_POSITIVE_HORN_OUTSIDE_FRAGMENT_V1;
        cetta_gdl_positive_horn_native_destroy_v1(native);
        return gdl_positive_horn_admission_v1(kind, NULL);
    }
    return gdl_positive_horn_admission_v1(
        CETTA_GDL_POSITIVE_HORN_ADMITTED_V1, native);
}

CettaGdlPositiveHornAdmissionV1
cetta_gdl_positive_horn_native_admit_v1(
    Atom *source_program,
    CettaGdlPositiveHornLimitsV1 limits) {
    return gdl_positive_horn_native_admit_mode_v1(
        source_program, limits, GDL_POSITIVE_HORN_SOURCE_ONLY_V1);
}

CettaGdlPositiveHornAdmissionV1
cetta_gdl_finite_view_native_admit_v1(
    Atom *source_program,
    CettaGdlPositiveHornLimitsV1 limits) {
    return gdl_positive_horn_native_admit_mode_v1(
        source_program, limits,
        GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1);
}

void cetta_gdl_positive_horn_native_destroy_v1(
    CettaGdlPositiveHornNativeV1 *native) {
    if (!native)
        return;
    cetta_gdl_stratification_destroy_v1(native->stratification);
    cetta_gdl_type_of_native_destroy_v1(native->typing);
    free(native->finite_domain.items);
    arena_free(&native->arena);
    free(native);
}

const CettaNikDirectAuthorityV1 *
cetta_gdl_positive_horn_native_authority_v1(void) {
    return &g_gdl_positive_horn_authority_v1;
}

const CettaNikDirectAuthorityV1 *
cetta_gdl_finite_view_native_authority_v1(void) {
    return &g_gdl_finite_view_authority_v1;
}

bool cetta_gdl_positive_horn_native_token_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    if (!native || !token_out)
        return false;
    *token_out = native->token;
    return true;
}

const CettaGdlStratificationV1 *
cetta_gdl_positive_horn_native_stratification_v1(
    const CettaGdlPositiveHornNativeV1 *native) {
    return native ? native->stratification : NULL;
}

bool cetta_gdl_positive_horn_native_token_is_current_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return native && token &&
        cetta_nik_direct_authority_token_v1_equal(&native->token, token);
}

bool cetta_gdl_positive_horn_native_identity_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    const char **source_sha256_out,
    const char **profile_sha256_out,
    const char **revision_out) {
    if (!native || !source_sha256_out || !profile_sha256_out ||
        !revision_out)
        return false;
    *source_sha256_out = native->package.source_sha256;
    *profile_sha256_out = native->package.profile_sha256;
    *revision_out = native->package.revision;
    return true;
}

static CettaGdlPositiveHornEpisodeAdmissionV1
gdl_positive_horn_episode_admission_v1(
    CettaGdlPositiveHornEpisodeAdmissionKindV1 kind,
    CettaGdlPositiveHornEpisodeV1 *episode) {
    return (CettaGdlPositiveHornEpisodeAdmissionV1){
        .kind = kind,
        .episode = episode,
    };
}

void cetta_gdl_positive_horn_episode_destroy_v1(
    CettaGdlPositiveHornEpisodeV1 *episode) {
    if (!episode)
        return;
    arena_free(&episode->arena);
    free(episode);
}

static bool gdl_positive_horn_is_authored_true_literal_v1(
    Atom *literal) {
    return literal && literal->kind == ATOM_EXPR &&
        literal->expr.len == 2u &&
        atom_is_symbol(literal->expr.elems[0], "true");
}

static bool gdl_positive_horn_has_authored_head_v1(
    Atom *literal, const char *head) {
    return literal && head && literal->kind == ATOM_EXPR &&
        literal->expr.len != 0u &&
        atom_is_symbol(literal->expr.elems[0], head);
}

static bool gdl_positive_horn_finite_domain_contains_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    Atom *structural_literal) {
    if (!native || !structural_literal)
        return false;
    for (size_t index = 0u;
         index < native->finite_domain.count; index++)
        if (atom_eq(
                native->finite_domain.items[index].structural_literal,
                structural_literal))
            return true;
    return false;
}

static CettaGdlPositiveHornEpisodeAdmissionKindV1
gdl_positive_horn_ground_episode_kind_v1(
    const CettaGdlTypeOfNativeGroundV1 *ground) {
    if (!ground)
        return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
    if (ground->kind == CETTA_GDL_TYPE_OF_NATIVE_GROUND_STALE_V1)
        return CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1;
    if (ground->kind ==
        CETTA_GDL_TYPE_OF_NATIVE_GROUND_ENGINE_FAULT_V1)
        return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
    if (ground->kind != CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1)
        return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
    switch (ground->value.outcome) {
    case CETTA_NIK_OUTCOME_ESTABLISHED:
        return ground->type && ground->proofs && ground->proof_count != 0u
            ? CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1
            : CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
    case CETTA_NIK_OUTCOME_REFUTED:
        return CETTA_GDL_POSITIVE_HORN_EPISODE_REFUTED_V1;
    case CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT:
        return CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1;
    case CETTA_NIK_OUTCOME_INCOMPLETE:
        return CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;
    default:
        return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
    }
}

static CettaGdlPositiveHornEpisodeAdmissionKindV1
gdl_positive_horn_construct_finite_absences_v1(
    CettaGdlPositiveHornNativeV1 *native,
    CettaGdlPositiveHornEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *typing_token,
    const GdlPositiveHornAtomsV1 *present_true,
    GdlPositiveHornAtomsV1 *blocks,
    CettaGdlPositiveHornEpisodeLimitsV1 limits) {
    if (!native || !episode || !typing_token || !present_true || !blocks ||
        native->mode != GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1)
        return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
    Atom *receipt_arguments[] = {
        episode->identity,
        atom_symbol(
            &episode->arena, native->package.calculus_input_sha256),
        atom_int(
            &episode->arena, (int64_t)native->finite_domain.count),
        atom_int(&episode->arena, (int64_t)episode->stats.authored_facts),
    };
    Atom *receipt = gdl_positive_horn_expr_v1(
        &episode->arena, "gdl:complete-finite-relation-view-v1",
        receipt_arguments, 4u);
    if (!receipt)
        return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;

    for (size_t domain_index = 0u;
         domain_index < native->finite_domain.count; domain_index++) {
        const GdlPositiveHornFiniteDomainMemberV1 *member =
            &native->finite_domain.items[domain_index];
        bool present = false;
        for (size_t present_index = 0u;
             present_index < present_true->count; present_index++)
            if (atom_eq(
                    member->structural_literal,
                    present_true->items[present_index]))
                present = true;
        if (present)
            continue;

        Atom *domain_source = atom_deep_copy(
            &episode->arena, member->source);
        Atom *occurrence_arguments[] = {
            receipt,
            domain_source,
            atom_int(&episode->arena, (int64_t)(domain_index + 1u)),
        };
        Atom *occurrence = gdl_positive_horn_expr_v1(
            &episode->arena, "gdl:finite-view-absence-occurrence-v1",
            occurrence_arguments, 3u);
        if (!domain_source || !occurrence)
            return CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
        CettaGdlTypeOfNativeGroundV1 ground =
            cetta_gdl_type_of_native_construct_ground_literal_v1(
                native->typing, typing_token, &episode->arena,
                occurrence, member->authored_literal,
                limits.max_typing_proofs_per_fact);
        CettaGdlPositiveHornEpisodeAdmissionKindV1 status =
            gdl_positive_horn_ground_episode_kind_v1(&ground);
        if (status != CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1)
            return status;
        if (ground.proof_count > limits.max_delta_blocks - blocks->count)
            return CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;

        for (size_t proof_index = 0u;
             proof_index < ground.proof_count; proof_index++) {
            Atom *source_arguments[] = {
                receipt, domain_source, ground.type,
                ground.proofs[proof_index],
            };
            Atom *source = gdl_positive_horn_expr_v1(
                &episode->arena, "gdl:typed-finite-absence-v1",
                source_arguments, 4u);
            Atom *structural_literal = atom_deep_copy(
                &episode->arena, member->structural_literal);
            Atom *proof_arguments[] = {source, structural_literal};
            Atom *proof = source && structural_literal
                ? gdl_positive_horn_expr_v1(
                    &episode->arena, "gdl:finite-absence-proof",
                    proof_arguments, 2u)
                : NULL;
            Atom *conclusion = atom_deep_copy(
                &episode->arena, member->absence_goal);
            char block_id_text[112];
            int block_id_length = snprintf(
                block_id_text, sizeof(block_id_text),
                "gdl-finite-absence-%06zu-proof-%06zu",
                domain_index + 1u, proof_index + 1u);
            Atom *block = block_id_length > 0 &&
                    (size_t)block_id_length < sizeof(block_id_text)
                ? gdl_positive_horn_block_v1(
                    &episode->arena,
                    atom_symbol(&episode->arena, block_id_text),
                    source, proof,
                    atom_symbol(&episode->arena, "rm-nil"),
                    conclusion)
                : NULL;
            if (!source || !proof || !conclusion || !block ||
                !gdl_positive_horn_atoms_push_v1(blocks, block))
                return CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;
            episode->stats.typing_proof_occurrences++;
            episode->stats.finite_state_absence_proof_occurrences++;
        }
    }
    return CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1;
}

static CettaGdlPositiveHornEpisodeAdmissionV1
gdl_positive_horn_native_admit_episode_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    CettaGdlPositiveHornEpisodeLimitsV1 limits,
    bool complete_finite_view) {
    if (!native || !source_token || !episode_identity ||
        (fact_count != 0u && !facts))
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    if (!cetta_gdl_positive_horn_native_token_is_current_v1(
            native, source_token))
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    if ((native->mode == GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1) !=
        complete_finite_view)
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1, NULL);
    if (atom_has_vars(episode_identity))
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1, NULL);
    if (limits.max_facts == 0u)
        limits.max_facts =
            GDL_POSITIVE_HORN_DEFAULT_MAX_EPISODE_FACTS_V1;
    if (limits.max_delta_blocks == 0u)
        limits.max_delta_blocks =
            GDL_POSITIVE_HORN_DEFAULT_MAX_EPISODE_BLOCKS_V1;
    if (fact_count > limits.max_facts)
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1, NULL);
    if (fact_count > (size_t)INT64_MAX)
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1, NULL);
    for (size_t index = 0u; index < fact_count; index++) {
        if (!facts[index])
            return gdl_positive_horn_episode_admission_v1(
                CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
        if (atom_has_vars(facts[index]))
            return gdl_positive_horn_episode_admission_v1(
                CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1, NULL);
    }

    CettaGdlPositiveHornEpisodeV1 *episode =
        cetta_malloc(sizeof(*episode));
    memset(episode, 0, sizeof(*episode));
    arena_init(&episode->arena);
    arena_set_runtime_kind(
        &episode->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    episode->native = native;
    episode->source_token = *source_token;
    episode->identity = atom_deep_copy(
        &episode->arena, episode_identity);
    if (!episode->identity) {
        cetta_gdl_positive_horn_episode_destroy_v1(episode);
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    }

    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    bool hashed = gdl_positive_horn_episode_digest_v1(
        native, &scratch, episode_identity, facts,
        fact_count, episode->digest);
    arena_free(&scratch);
    char revision_text[96];
    int revision_length = hashed
        ? snprintf(
            revision_text, sizeof(revision_text),
            "gdl-typed-episode-%s", episode->digest)
        : -1;
    episode->revision = revision_length > 0 &&
            (size_t)revision_length < sizeof(revision_text)
        ? arena_strdup(&episode->arena, revision_text)
        : NULL;
    if (!episode->revision ||
        !cetta_nik_direct_authority_v1_token_from_sha256(
            native->authority, episode->digest,
            2u, &episode->token)) {
        cetta_gdl_positive_horn_episode_destroy_v1(episode);
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    }

    CettaNikDirectAuthorityTokenV1 typing_token = {0};
    if (!cetta_gdl_type_of_native_token_v1(
            native->typing, &typing_token) ||
        !cetta_gdl_type_of_native_token_is_current_v1(
            native->typing, &typing_token)) {
        cetta_gdl_positive_horn_episode_destroy_v1(episode);
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    }

    GdlPositiveHornAtomsV1 blocks = {0};
    GdlPositiveHornAtomsV1 present_true = {0};
    CettaGdlPositiveHornEpisodeAdmissionKindV1 status =
        CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1;
    for (size_t fact_index = 0u;
         fact_index < fact_count; fact_index++) {
        Atom *occurrence_arguments[] = {
            episode->identity,
            atom_int(&episode->arena, (int64_t)(fact_index + 1u)),
        };
        Atom *occurrence = gdl_positive_horn_expr_v1(
            &episode->arena, "gdl:episode-occurrence",
            occurrence_arguments, 2u);
        if (!occurrence) {
            status = CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
            break;
        }
        CettaGdlTypeOfNativeGroundV1 ground =
            cetta_gdl_type_of_native_construct_ground_literal_v1(
                native->typing, &typing_token, &episode->arena,
                occurrence, facts[fact_index],
                limits.max_typing_proofs_per_fact);
        status = gdl_positive_horn_ground_episode_kind_v1(&ground);
        if (status != CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1)
            break;
        if (ground.proof_count >
            limits.max_delta_blocks - blocks.count) {
            status = CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;
            break;
        }
        Atom *structural = !atom_has_vars(facts[fact_index])
            ? atom_deep_copy(&episode->arena, facts[fact_index]) : NULL;
        if (!structural) {
            status =
                CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1;
            break;
        }
        if (native->mode == GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1 &&
            gdl_positive_horn_has_authored_head_v1(
                facts[fact_index], "true")) {
            if (!gdl_positive_horn_is_authored_true_literal_v1(
                    facts[fact_index]) ||
                !gdl_positive_horn_finite_domain_contains_v1(
                    native, structural)) {
                status =
                    CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1;
                break;
            }
            if (!gdl_positive_horn_atoms_push_v1(
                    &present_true, structural)) {
                status = CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;
                break;
            }
        }
        for (size_t proof_index = 0u;
             proof_index < ground.proof_count; proof_index++) {
            Atom *source_arguments[] = {
                occurrence, ground.type, ground.proofs[proof_index],
            };
            Atom *source = gdl_positive_horn_expr_v1(
                &episode->arena, "gdl:typed-episode-occurrence",
                source_arguments, 3u);
            Atom *proof_arguments[] = {source, structural};
            Atom *proof = source
                ? gdl_positive_horn_expr_v1(
                    &episode->arena, "gdl:episode-fact",
                    proof_arguments, 2u)
                : NULL;
            char block_id_text[96];
            int block_id_length = snprintf(
                block_id_text, sizeof(block_id_text),
                "gdl-episode-%06zu-proof-%06zu",
                fact_index + 1u, proof_index + 1u);
            Atom *block = block_id_length > 0 &&
                    (size_t)block_id_length < sizeof(block_id_text)
                ? gdl_positive_horn_block_v1(
                    &episode->arena,
                    atom_symbol(&episode->arena, block_id_text),
                    source, proof,
                    atom_symbol(&episode->arena, "rm-nil"), structural)
                : NULL;
            if (!source || !proof || !block ||
                !gdl_positive_horn_atoms_push_v1(&blocks, block)) {
                status = CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;
                break;
            }
            episode->stats.typing_proof_occurrences++;
        }
        if (status != CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1)
            break;
        episode->stats.authored_facts++;
    }

    if (status == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
        native->mode == GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1)
        status = gdl_positive_horn_construct_finite_absences_v1(
            native, episode, &typing_token, &present_true,
            &blocks, limits);

    if (status == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
        blocks.count > (size_t)UINT32_MAX - 1u) {
        status = CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1;
    }
    if (status == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1) {
        Atom **package_items = arena_alloc(
            &episode->arena, (blocks.count + 1u) * sizeof(*package_items));
        package_items[0] = atom_symbol(&episode->arena, "rm-package");
        for (size_t index = 0u; index < blocks.count; index++)
            package_items[index + 1u] = blocks.items[index];
        Atom *package = atom_expr(
            &episode->arena, package_items,
            (CettaExprLen)(blocks.count + 1u));
        Atom *head = atom_symbol(&episode->arena, "compile:link-package");
        Atom *arguments[] = {
            native->base_artifact,
            atom_symbol(&episode->arena, episode->revision),
            package,
        };
        episode->artifact = package && arguments[1]
            ? cetta_rule_machine_dispatch(
                &episode->arena, head, arguments, 3u)
            : NULL;
        if (!episode->artifact ||
            gdl_positive_horn_error_v1(episode->artifact))
            status = CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1;
        else
            episode->stats.compiled_delta_blocks = blocks.count;
    }
    free(present_true.items);
    free(blocks.items);
    if (status != CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1) {
        cetta_gdl_positive_horn_episode_destroy_v1(episode);
        return gdl_positive_horn_episode_admission_v1(status, NULL);
    }
    return gdl_positive_horn_episode_admission_v1(status, episode);
}

CettaGdlPositiveHornEpisodeAdmissionV1
cetta_gdl_positive_horn_native_admit_episode_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    CettaGdlPositiveHornEpisodeLimitsV1 limits) {
    return gdl_positive_horn_native_admit_episode_v1(
        native, source_token, episode_identity, facts,
        fact_count, limits, false);
}

static Atom *gdl_positive_horn_space_u64_v1(
    Arena *arena, uint64_t value) {
    char text[17];
    int length = snprintf(text, sizeof(text), "%016" PRIx64, value);
    return length == 16 ? atom_string(arena, text) : NULL;
}

CettaGdlPositiveHornEpisodeAdmissionV1
cetta_gdl_finite_view_native_admit_space_episode_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Space *episode_space,
    Atom *episode_identity,
    CettaGdlPositiveHornEpisodeLimitsV1 limits) {
    if (!native || !source_token || !episode_space || !episode_identity ||
        native->mode != GDL_POSITIVE_HORN_FINITE_TRUE_VIEW_V1 ||
        space_instance_id(episode_space) == 0u)
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1, NULL);
    if (!cetta_gdl_positive_horn_native_token_is_current_v1(
            native, source_token))
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);

    SpaceReadToken read = space_read_token(episode_space);
    if (!space_read_token_matches_live_space(read, episode_space))
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    CettaCount fact_count64 = space_length64(episode_space);
    size_t fact_count = (size_t)fact_count64;
    if ((CettaCount)fact_count != fact_count64)
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1, NULL);
    Atom **facts = fact_count
        ? malloc(fact_count * sizeof(*facts)) : NULL;
    if (fact_count && !facts)
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);

    Arena identity_arena;
    arena_init(&identity_arena);
    arena_set_runtime_kind(
        &identity_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    Atom **occurrences = fact_count
        ? arena_alloc(
            &identity_arena, fact_count * sizeof(*occurrences))
        : NULL;
    bool represented = true;
    for (size_t index = 0u; index < fact_count; index++) {
        facts[index] = space_get_at64(episode_space, (CettaIndex)index);
        AtomId atom_id = space_get_atom_id_at64(
            episode_space, (CettaIndex)index);
        Atom *occurrence_arguments[] = {
            gdl_positive_horn_space_u64_v1(
                &identity_arena, (uint64_t)index),
            atom_id == CETTA_ATOM_ID_NONE
                ? atom_symbol(&identity_arena, "no-atom-id")
                : gdl_positive_horn_space_u64_v1(
                    &identity_arena, (uint64_t)atom_id),
        };
        occurrences[index] = facts[index] && occurrence_arguments[0] &&
                occurrence_arguments[1]
            ? gdl_positive_horn_expr_v1(
                &identity_arena, "gdl:space-fact-occurrence-v1",
                occurrence_arguments, 2u)
            : NULL;
        if (!occurrences[index]) {
            represented = false;
            break;
        }
    }
    Atom *occurrence_bag = represented
        ? gdl_positive_horn_expr_v1(
            &identity_arena, "gdl:space-fact-occurrences-v1",
            occurrences, fact_count)
        : NULL;
    Atom *read_arguments[] = {
        gdl_positive_horn_space_u64_v1(
            &identity_arena, read.instance_id),
        gdl_positive_horn_space_u64_v1(
            &identity_arena, read.revision),
    };
    Atom *read_receipt = read_arguments[0] && read_arguments[1]
        ? gdl_positive_horn_expr_v1(
            &identity_arena, "gdl:space-read-v1",
            read_arguments, 2u)
        : NULL;
    Atom *identity_arguments[] = {
        atom_deep_copy(&identity_arena, episode_identity),
        read_receipt,
        occurrence_bag,
    };
    Atom *complete_identity = represented && identity_arguments[0] &&
            identity_arguments[1] && identity_arguments[2]
        ? gdl_positive_horn_expr_v1(
            &identity_arena, "gdl:space-complete-finite-view-v1",
            identity_arguments, 3u)
        : NULL;
    CettaGdlPositiveHornEpisodeAdmissionV1 admitted = complete_identity
        ? gdl_positive_horn_native_admit_episode_v1(
            native, source_token, complete_identity, facts,
            fact_count, limits, true)
        : gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1, NULL);
    free(facts);
    arena_free(&identity_arena);

    if (!space_read_token_matches_live_space(read, episode_space)) {
        cetta_gdl_positive_horn_episode_destroy_v1(admitted.episode);
        return gdl_positive_horn_episode_admission_v1(
            CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1, NULL);
    }
    if (admitted.kind == CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
        admitted.episode) {
        admitted.episode->complete_space = episode_space;
        admitted.episode->complete_read = read;
        admitted.episode->has_complete_space_read = true;
    }
    return admitted;
}

bool cetta_gdl_positive_horn_episode_token_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    if (!episode || !token_out)
        return false;
    *token_out = episode->token;
    return true;
}

bool cetta_gdl_positive_horn_episode_token_is_current_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *token) {
    return episode && episode->native && token &&
        cetta_gdl_positive_horn_native_token_is_current_v1(
            episode->native, &episode->source_token) &&
        (!episode->has_complete_space_read ||
         (episode->complete_space &&
          space_read_token_matches_live_space(
              episode->complete_read, episode->complete_space))) &&
        cetta_nik_direct_authority_token_v1_equal(
            &episode->token, token);
}

bool cetta_gdl_positive_horn_episode_identity_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    const char **digest_out,
    const char **revision_out) {
    if (!episode || !digest_out || !revision_out)
        return false;
    *digest_out = episode->digest;
    *revision_out = episode->revision;
    return true;
}

static CettaGdlPositiveHornRunV1 gdl_positive_horn_run_artifact_v1(
    Atom *artifact,
    uint64_t implementation_identity,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences) {
    if (!artifact || !result_arena || !query || max_states == 0u ||
        max_states > INT64_MAX || max_occurrences == 0u)
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
            .implementation_identity = implementation_identity,
        };
    Atom *head = atom_symbol(result_arena, "compile:run");
    Atom *query_copy = atom_deep_copy(result_arena, query);
    Atom *quoted_query = gdl_positive_horn_quote_v1(
        result_arena, query_copy);
    Atom *arguments[] = {
        artifact,
        atom_int(result_arena, depth),
        atom_int(result_arena, (int64_t)max_states),
        atom_int(result_arena, max_occurrences),
        quoted_query,
    };
    if (!head || !arguments[1] || !arguments[2] || !arguments[3] ||
        !arguments[4])
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
            .implementation_identity = implementation_identity,
        };
    Atom *result = cetta_rule_machine_dispatch(
        result_arena, head, arguments, 5u);
    if (!result || gdl_positive_horn_error_v1(result))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
            .implementation_identity = implementation_identity,
            .result = result,
        };
    if (result->kind == ATOM_EXPR && result->expr.len == 6u &&
        atom_is_symbol(result->expr.elems[0], "compile-incomplete"))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_INCOMPLETE_V1,
            .implementation_identity = implementation_identity,
            .result = result,
        };
    if (result->kind == ATOM_EXPR && result->expr.len == 5u &&
        atom_is_symbol(result->expr.elems[0], "compile-result"))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1,
            .implementation_identity = implementation_identity,
            .result = result,
        };
    return (CettaGdlPositiveHornRunV1){
        .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
        .implementation_identity = implementation_identity,
        .result = result,
    };
}

CettaGdlPositiveHornRunV1 cetta_gdl_positive_horn_native_run_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences) {
    if (!native || !token)
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
        };
    if (!cetta_gdl_positive_horn_native_token_is_current_v1(native, token))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1,
        };
    return gdl_positive_horn_run_artifact_v1(
        native->base_artifact, native->authority->realization_identity,
        result_arena, query,
        depth, max_states, max_occurrences);
}

CettaGdlPositiveHornRunV1 cetta_gdl_positive_horn_episode_run_v1(
    CettaGdlPositiveHornEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *token,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences) {
    if (!episode || !token)
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
        };
    if (!cetta_gdl_positive_horn_episode_token_is_current_v1(
            episode, token))
        return (CettaGdlPositiveHornRunV1){
            .kind = CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1,
        };
    return gdl_positive_horn_run_artifact_v1(
        episode->artifact, episode->native->authority->realization_identity,
        result_arena, query,
        depth, max_states, max_occurrences);
}

bool cetta_gdl_positive_horn_native_stats_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    CettaGdlPositiveHornStatsV1 *stats_out) {
    if (!native || !stats_out)
        return false;
    *stats_out = native->stats;
    return true;
}

bool cetta_gdl_positive_horn_episode_stats_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    CettaGdlPositiveHornEpisodeStatsV1 *stats_out) {
    if (!episode || !stats_out)
        return false;
    *stats_out = episode->stats;
    return true;
}
