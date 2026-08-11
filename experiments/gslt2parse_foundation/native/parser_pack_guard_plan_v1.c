#include "parser_pack_guard_plan_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PPGUARD_PLAN_V1_MAX_TERM_DEPTH = 4096u };

typedef enum {
    PPGUARD_PLAN_V1_BACKEND_GLL = 0,
    PPGUARD_PLAN_V1_BACKEND_GLR = 1
} PPGuardPlanV1Backend;

typedef struct {
    const PPGuardPlanV1DerivationInput *input;
    Atom *owner;
    Atom *state;
    Atom *tag;
    Atom *body;
    Atom *production;
    Atom *terminal;
    char *state_canonical;
    char *answer_canonical;
    char *certificate_canonical;
    char *evidence_canonical;
} PPGuardPlanV1RawDerivation;

static void ppguard_plan_v1_set_error(char *buf,
                                      size_t size,
                                      const char *format,
                                      ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppguard_plan_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool ppguard_plan_v1_array_fits(size_t count,
                                       size_t element_size) {
    return element_size == 0u || count <= SIZE_MAX / element_size;
}

static bool ppguard_plan_v1_expr_head(const Atom *atom,
                                      const char *head,
                                      CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static char *ppguard_plan_v1_render(const Atom *atom) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(atom, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static char *ppguard_plan_v1_text_dup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (copy)
        memcpy(copy, text, len + 1u);
    return copy;
}

static bool ppguard_plan_v1_state_owned_by(const Atom *state,
                                           const Atom *owner,
                                           unsigned depth) {
    if (depth > PPGUARD_PLAN_V1_MAX_TERM_DEPTH)
        return false;
    if (ppguard_plan_v1_expr_head(state, "pp-def", 1u))
        return atom_eq(state->expr.elems[1], (Atom *)owner);
    return ppguard_plan_v1_expr_head(state, "pp-sub", 2u) &&
        ppguard_plan_v1_state_owned_by(
            state->expr.elems[1], owner, depth + 1u);
}

static bool ppguard_plan_v1_answer_fields(
    Atom *answer,
    Atom **owner,
    Atom **state,
    Atom **tag,
    Atom **body,
    Atom **production,
    Atom **terminal) {
    Atom *artifact;
    Atom *label;
    Atom *items;
    Atom *item;
    Atom *action;

    if (!ppguard_plan_v1_expr_head(
            answer, "compile-pack-positive-guard", 2u)) {
        return false;
    }
    *owner = answer->expr.elems[1];
    artifact = answer->expr.elems[2];
    if (!ppguard_plan_v1_expr_head(
            artifact, "pp-positive-guard-v1", 4u)) {
        return false;
    }
    *state = artifact->expr.elems[1];
    *tag = artifact->expr.elems[2];
    *body = artifact->expr.elems[3];
    *production = artifact->expr.elems[4];
    if (!ppguard_plan_v1_state_owned_by(*state, *owner, 0u) ||
        !ppguard_plan_v1_expr_head(
            *tag, "pp-positive-guard-tag", 1u) ||
        !atom_eq((*tag)->expr.elems[1], *state) ||
        !ppguard_plan_v1_expr_head(
            *production, "pp-production", 4u)) {
        return false;
    }
    label = (*production)->expr.elems[1];
    items = (*production)->expr.elems[3];
    action = (*production)->expr.elems[4];
    if (!ppguard_plan_v1_expr_head(label, "pp-label", 2u) ||
        !atom_eq(label->expr.elems[1], *state) ||
        !atom_is_symbol(label->expr.elems[2], "peek") ||
        !atom_eq((*production)->expr.elems[2], *state) ||
        !ppguard_plan_v1_expr_head(items, "pp-items-cons", 2u) ||
        !atom_is_symbol(items->expr.elems[2], "pp-items-nil") ||
        !ppguard_plan_v1_expr_head(action, "pa-slot", 1u) ||
        !atom_is_symbol(action->expr.elems[1], "q-zero")) {
        return false;
    }
    item = items->expr.elems[1];
    if (!ppguard_plan_v1_expr_head(item, "pp-terminal", 1u) ||
        !ppguard_plan_v1_expr_head(
            item->expr.elems[1], "pp-span-terminal", 1u) ||
        !atom_eq(item->expr.elems[1]->expr.elems[1], *tag)) {
        return false;
    }
    *terminal = item->expr.elems[1];
    return true;
}

static int ppguard_plan_v1_raw_compare(const void *left,
                                       const void *right) {
    const PPGuardPlanV1RawDerivation *lhs = left;
    const PPGuardPlanV1RawDerivation *rhs = right;
    int comparison = strcmp(lhs->state_canonical, rhs->state_canonical);

    if (comparison != 0)
        return comparison;
    comparison = strcmp(lhs->answer_canonical, rhs->answer_canonical);
    if (comparison != 0)
        return comparison;
    return strcmp(lhs->certificate_canonical,
                  rhs->certificate_canonical);
}

static int ppguard_plan_v1_text_pointer_compare(const void *left,
                                                const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static char *ppguard_plan_v1_evidence_key(const char *answer,
                                          const char *certificate) {
    size_t answer_len = strlen(answer);
    size_t certificate_len = strlen(certificate);
    char *key;

    if (answer_len > SIZE_MAX - certificate_len - 2u)
        return NULL;
    key = malloc(answer_len + certificate_len + 2u);
    if (!key)
        return NULL;
    memcpy(key, answer, answer_len);
    key[answer_len] = '\n';
    memcpy(key + answer_len + 1u, certificate, certificate_len + 1u);
    return key;
}

static void ppguard_plan_v1_raw_free(PPGuardPlanV1RawDerivation *raw,
                                     size_t len) {
    size_t index;

    if (!raw)
        return;
    for (index = 0u; index < len; index++) {
        free(raw[index].state_canonical);
        free(raw[index].answer_canonical);
        free(raw[index].certificate_canonical);
        free(raw[index].evidence_canonical);
    }
    free(raw);
}

static bool ppguard_plan_v1_answer_set_digest(
    Atom *const *answers,
    uint32_t answer_len,
    char out[65]) {
    static const char domain[] = "FiniteHornAnswerSetV1\n";
    CettaNativeSha256 sha;
    char **canonical = NULL;
    uint32_t index;
    bool ok = false;

    if (answer_len > 0u) {
        canonical = calloc(answer_len, sizeof(*canonical));
        if (!canonical)
            goto done;
    }
    for (index = 0u; index < answer_len; index++) {
        canonical[index] = ppguard_plan_v1_render(answers[index]);
        if (!canonical[index])
            goto done;
    }
    if (answer_len > 1u) {
        qsort(canonical, answer_len, sizeof(*canonical),
              ppguard_plan_v1_text_pointer_compare);
    }
    for (index = 1u; index < answer_len; index++) {
        if (strcmp(canonical[index - 1u], canonical[index]) == 0)
            goto done;
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    for (index = 0u; index < answer_len; index++) {
        cetta_native_sha256_update(
            &sha, (const uint8_t *)canonical[index],
            strlen(canonical[index]));
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    ok = true;

done:
    if (canonical) {
        for (index = 0u; index < answer_len; index++)
            free(canonical[index]);
    }
    free(canonical);
    return ok;
}

static void ppguard_plan_v1_sha_u32(CettaNativeSha256 *sha,
                                    uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppguard_plan_v1_sha_bytes(CettaNativeSha256 *sha,
                                      const uint8_t *bytes,
                                      size_t len) {
    uint64_t value = (uint64_t)len;
    uint8_t length[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        length[7u - index] = (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, length, sizeof(length));
    cetta_native_sha256_update(sha, bytes, len);
}

static void ppguard_plan_v1_sha_text(CettaNativeSha256 *sha,
                                     const char *text) {
    ppguard_plan_v1_sha_bytes(
        sha, (const uint8_t *)text, strlen(text));
}

static bool ppguard_plan_v1_sha_atom(CettaNativeSha256 *sha,
                                     const Atom *atom) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    bool ok = fh_ground_term_v1_render(
        atom, &canonical, &canonical_len, NULL, 0u);

    if (ok)
        ppguard_plan_v1_sha_bytes(sha, canonical, canonical_len);
    free(canonical);
    return ok;
}

static bool ppguard_plan_v1_compute_digest(const PPGuardPlanV1 *plan,
                                           char out[65]) {
    static const char domain[] = "ParserPackPositiveGuardPlanV1";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppguard_plan_v1_sha_bytes(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppguard_plan_v1_sha_text(&sha, plan->base_pack_digest);
    ppguard_plan_v1_sha_u32(&sha, plan->has_lexical_plan ? 1u : 0u);
    if (plan->has_lexical_plan)
        ppguard_plan_v1_sha_text(&sha, plan->lexical_plan_digest);
    ppguard_plan_v1_sha_text(&sha, plan->source_digest);
    ppguard_plan_v1_sha_text(&sha, plan->pre_reflection_digest);
    ppguard_plan_v1_sha_text(&sha, plan->environment_digest);
    ppguard_plan_v1_sha_text(&sha, plan->answer_set_digest);
    ppguard_plan_v1_sha_text(&sha, plan->regular_compiler_digest);
    ppguard_plan_v1_sha_text(&sha, plan->guard_nfa_answer_digest);
    ppguard_plan_v1_sha_u32(&sha, plan->lexical_terminal_len);
    ppguard_plan_v1_sha_u32(&sha, plan->entry_len);
    for (index = 0u; index < plan->entry_len; index++) {
        const PPGuardPlanV1Entry *entry = &plan->entries[index];
        ppguard_plan_v1_sha_u32(&sha, entry->state_id);
        ppguard_plan_v1_sha_u32(&sha, entry->terminal_id);
        ppguard_plan_v1_sha_u32(&sha, entry->production_id);
        ppguard_plan_v1_sha_u32(
            &sha, entry->state_is_extension ? 1u : 0u);
        if (!ppguard_plan_v1_sha_atom(&sha, entry->owner) ||
            !ppguard_plan_v1_sha_atom(&sha, entry->state) ||
            !ppguard_plan_v1_sha_atom(&sha, entry->tag) ||
            !ppguard_plan_v1_sha_atom(&sha, entry->body) ||
            !ppguard_plan_v1_sha_atom(&sha, entry->production)) {
            return false;
        }
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

static bool ppguard_plan_v1_compute_evidence_digest(
    const PPGuardPlanV1 *plan,
    char out[65]) {
    static const char domain[] = "ParserPackPositiveGuardEvidenceV1";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppguard_plan_v1_sha_bytes(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppguard_plan_v1_sha_text(&sha, plan->plan_digest);
    ppguard_plan_v1_sha_u32(&sha, plan->derivation_len);
    for (index = 0u; index < plan->derivation_len; index++) {
        const PPGuardPlanV1Derivation *derivation =
            &plan->derivations[index];
        ppguard_plan_v1_sha_u32(&sha, derivation->entry_index);
        if (!ppguard_plan_v1_sha_atom(&sha, derivation->answer) ||
            !ppguard_plan_v1_sha_atom(&sha, derivation->certificate)) {
            return false;
        }
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

void ppguard_plan_v1_init(PPGuardPlanV1 *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    arena_init(&plan->arena);
}

void ppguard_plan_v1_free(PPGuardPlanV1 *plan) {
    uint32_t index;

    if (!plan)
        return;
    for (index = 0u; index < plan->entry_len; index++)
        free(plan->entries[index].state_canonical);
    for (index = 0u; index < plan->derivation_len; index++)
        free(plan->derivations[index].canonical);
    free(plan->entries);
    free(plan->derivations);
    free(plan->states);
    free(plan->terminals);
    free(plan->productions);
    free(plan->production_items);
    free(plan->guard_terminal_ids);
    arena_free(&plan->arena);
    memset(plan, 0, sizeof(*plan));
}

static bool ppguard_plan_v1_lexical_validate(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGrammar grammar;
    bool ok;

    cetta_lp_native_grammar_init(&grammar);
    ok = pplex_v1_grammar_build(
        pack, lexical_plan, &grammar, error_buf, error_buf_size);
    cetta_lp_native_grammar_free(&grammar);
    return ok;
}

static bool ppguard_plan_v1_guard_tags_bijective(
    const PPGuardPlanV1 *plan,
    Atom *const *guard_nfa_tags,
    size_t guard_nfa_tag_len,
    char *error_buf,
    size_t error_buf_size) {
    uint8_t *matched = NULL;
    size_t tag_index;
    bool ok = false;

    if (!plan || guard_nfa_tag_len != plan->entry_len ||
        (guard_nfa_tag_len > 0u && !guard_nfa_tags)) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "positive guard NFA tag inventory is not bijective");
        return false;
    }
    if (plan->entry_len == 0u)
        return true;
    matched = calloc(plan->entry_len, sizeof(*matched));
    if (!matched)
        return false;
    for (tag_index = 0u; tag_index < guard_nfa_tag_len; tag_index++) {
        char *canonical = ppguard_plan_v1_render(
            guard_nfa_tags[tag_index]);
        uint32_t entry_index;
        uint32_t matches = 0u;

        if (!canonical) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard NFA tag is not ground");
            goto done;
        }
        free(canonical);
        for (entry_index = 0u; entry_index < plan->entry_len;
             entry_index++) {
            if (!atom_eq(guard_nfa_tags[tag_index],
                         plan->entries[entry_index].tag)) {
                continue;
            }
            matches++;
            if (matched[entry_index])
                matches++;
            matched[entry_index] = 1u;
        }
        if (matches != 1u) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard NFA tag inventory is not bijective");
            goto done;
        }
    }
    for (tag_index = 0u; tag_index < plan->entry_len; tag_index++) {
        if (!matched[tag_index]) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard NFA omits one planned guard body");
            goto done;
        }
    }
    ok = true;

done:
    free(matched);
    return ok;
}

static bool ppguard_plan_v1_matches(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1ForestExtension extension;
    Atom **answers = NULL;
    char answer_digest[65];
    char plan_digest[65];
    char evidence_digest[65];
    uint32_t extension_state_index = 0u;
    uint32_t evidence_index = 0u;
    uint32_t index;
    bool ok = false;

    if (!pack || !plan ||
        plan->production_len != plan->entry_len ||
        plan->entry_len > UINT32_MAX - plan->lexical_terminal_len ||
        plan->terminal_len != plan->lexical_terminal_len + plan->entry_len ||
        plan->state_len > plan->entry_len ||
        (plan->entry_len > 0u && (!plan->entries ||
         !plan->productions || !plan->production_items ||
         !plan->guard_terminal_ids)) ||
        (plan->derivation_len > 0u && !plan->derivations) ||
        (plan->terminal_len > 0u && !plan->terminals) ||
        (plan->state_len > 0u && !plan->states) ||
        !ppguard_plan_v1_digest_valid(plan->base_pack_digest) ||
        !ppguard_plan_v1_digest_valid(plan->source_digest) ||
        !ppguard_plan_v1_digest_valid(plan->pre_reflection_digest) ||
        !ppguard_plan_v1_digest_valid(plan->environment_digest) ||
        !ppguard_plan_v1_digest_valid(plan->answer_set_digest) ||
        !ppguard_plan_v1_digest_valid(plan->regular_compiler_digest) ||
        !ppguard_plan_v1_digest_valid(plan->guard_nfa_answer_digest) ||
        !ppguard_plan_v1_digest_valid(plan->plan_digest) ||
        !ppguard_plan_v1_digest_valid(plan->evidence_digest) ||
        strcmp(pack->pack_digest, plan->base_pack_digest) != 0 ||
        plan->entry_len > UINT32_MAX - pack->production_len ||
        plan->terminal_len > UINT32_MAX - pack->terminal_len ||
        plan->state_len > UINT32_MAX - pack->state_len) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "malformed positive guard plan");
        goto done;
    }
    if (plan->has_lexical_plan) {
        if (!lexical_plan ||
            !ppguard_plan_v1_digest_valid(plan->lexical_plan_digest) ||
            strcmp(lexical_plan->plan_digest,
                   plan->lexical_plan_digest) != 0 ||
            lexical_plan->entry_len != plan->lexical_terminal_len ||
            !ppguard_plan_v1_lexical_validate(
                pack, lexical_plan, error_buf, error_buf_size)) {
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard lexical plan does not match");
            }
            goto done;
        }
    } else if (lexical_plan || plan->lexical_terminal_len != 0u ||
               plan->lexical_plan_digest[0] != '\0') {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "positive guard plan has an unexpected lexical projection");
        goto done;
    }
    for (index = 0u; index < plan->lexical_terminal_len; index++) {
        if (plan->terminals[index].terminal_id !=
                pack->terminal_len + index ||
            !atom_eq(plan->terminals[index].identity,
                     lexical_plan->terminals[index].identity)) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard lexical terminal identity changed");
            goto done;
        }
    }
    if (plan->entry_len > 0u) {
        answers = calloc(plan->entry_len, sizeof(*answers));
        if (!answers)
            goto done;
    }
    extension = (PPNativeV1ForestExtension){
        .states = plan->states,
        .state_len = plan->state_len,
        .terminals = plan->terminals,
        .terminal_len = plan->terminal_len,
        .productions = plan->productions,
        .production_len = plan->production_len,
    };
    for (index = 0u; index < plan->entry_len; index++) {
        const PPGuardPlanV1Entry *entry = &plan->entries[index];
        const PPNativeV1TerminalExtension *terminal =
            &plan->terminals[plan->lexical_terminal_len + index];
        const PPNativeV1ProductionExtension *production =
            &plan->productions[index];
        Atom *owner;
        Atom *state;
        Atom *tag;
        Atom *body;
        Atom *production_identity;
        Atom *terminal_identity;
        char *state_canonical;
        int32_t base_state;
        uint32_t derivation_index;

        if (!entry->owner || !entry->state || !entry->tag || !entry->body ||
            !entry->production || !entry->state_canonical ||
            entry->terminal_id !=
                pack->terminal_len + plan->lexical_terminal_len + index ||
            entry->production_id != pack->production_len + index ||
            plan->guard_terminal_ids[index] != entry->terminal_id ||
            entry->evidence_begin != evidence_index ||
            entry->evidence_len == 0u ||
            entry->evidence_len > plan->derivation_len - evidence_index ||
            terminal->terminal_id != entry->terminal_id ||
            production->production_id != entry->production_id ||
            production->lhs_state_id != entry->state_id ||
            production->item_len != 1u ||
            production->items != &plan->production_items[index] ||
            production->items[0].kind != PPABI_V1_ITEM_TERMINAL ||
            production->items[0].dense_id != entry->terminal_id ||
            !atom_eq(production->identity, entry->production)) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard entry is inconsistent");
            goto done;
        }
        state_canonical = ppguard_plan_v1_render(entry->state);
        if (!state_canonical ||
            strcmp(state_canonical, entry->state_canonical) != 0 ||
            (index > 0u &&
             strcmp(plan->entries[index - 1u].state_canonical,
                    entry->state_canonical) >= 0)) {
            free(state_canonical);
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard states are not canonical and unique");
            goto done;
        }
        free(state_canonical);
        base_state = ppnative_v1_state_find(pack, entry->state);
        if (base_state >= 0) {
            if (entry->state_is_extension ||
                entry->state_id != (uint32_t)base_state ||
                pack->states[base_state].defined) {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard state no longer denotes one open state");
                goto done;
            }
        } else {
            if (!entry->state_is_extension ||
                extension_state_index >= plan->state_len ||
                entry->state_id != pack->state_len + extension_state_index ||
                plan->states[extension_state_index].state_id !=
                    entry->state_id ||
                !atom_eq(plan->states[extension_state_index].identity,
                         entry->state)) {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard extended state mapping changed");
                goto done;
            }
            extension_state_index++;
        }
        if (!ppnative_v1_start_is_closed_extended(
                pack, entry->state, &extension,
                error_buf, error_buf_size)) {
            goto done;
        }
        if (!ppguard_plan_v1_answer_fields(
                plan->derivations[evidence_index].answer,
                &owner, &state, &tag, &body,
                &production_identity, &terminal_identity) ||
            !atom_eq(owner, entry->owner) ||
            !atom_eq(state, entry->state) ||
            !atom_eq(tag, entry->tag) ||
            !atom_eq(body, entry->body) ||
            !atom_eq(production_identity, entry->production) ||
            !atom_eq(terminal_identity, terminal->identity) ||
            !atom_eq(production->action,
                     production_identity->expr.elems[4])) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard derivation does not identify its entry");
            goto done;
        }
        answers[index] = plan->derivations[evidence_index].answer;
        for (derivation_index = 0u;
             derivation_index < entry->evidence_len;
             derivation_index++) {
            const PPGuardPlanV1Derivation *derivation =
                &plan->derivations[evidence_index + derivation_index];
            char *answer_text;
            char *certificate_text;
            char *evidence_text;

            if (derivation->entry_index != index || !derivation->answer ||
                !derivation->certificate || !derivation->canonical ||
                !ppguard_plan_v1_answer_fields(
                    derivation->answer, &owner, &state, &tag, &body,
                    &production_identity, &terminal_identity) ||
                !atom_eq(owner, entry->owner) ||
                !atom_eq(state, entry->state) ||
                !atom_eq(tag, entry->tag) ||
                !atom_eq(body, entry->body) ||
                !atom_eq(production_identity, entry->production)) {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard evidence range is inconsistent");
                goto done;
            }
            answer_text = ppguard_plan_v1_render(derivation->answer);
            certificate_text =
                ppguard_plan_v1_render(derivation->certificate);
            evidence_text = answer_text && certificate_text
                ? ppguard_plan_v1_evidence_key(
                      answer_text, certificate_text)
                : NULL;
            free(answer_text);
            free(certificate_text);
            if (!evidence_text ||
                strcmp(evidence_text, derivation->canonical) != 0 ||
                (derivation_index > 0u &&
                 strcmp(plan->derivations[
                            evidence_index + derivation_index - 1u].canonical,
                        derivation->canonical) >= 0)) {
                free(evidence_text);
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard evidence is not canonical and unique");
                goto done;
            }
            free(evidence_text);
        }
        evidence_index += entry->evidence_len;
    }
    if (extension_state_index != plan->state_len ||
        evidence_index != plan->derivation_len ||
        !ppguard_plan_v1_answer_set_digest(
            answers, plan->entry_len, answer_digest) ||
        strcmp(answer_digest, plan->answer_set_digest) != 0 ||
        !ppguard_plan_v1_compute_digest(plan, plan_digest) ||
        strcmp(plan_digest, plan->plan_digest) != 0 ||
        !ppguard_plan_v1_compute_evidence_digest(
            plan, evidence_digest) ||
        strcmp(evidence_digest, plan->evidence_digest) != 0) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "positive guard plan digest or inventory changed");
        goto done;
    }
    ok = true;

done:
    free(answers);
    return ok;
}

bool ppguard_plan_v1_validate(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    return ppguard_plan_v1_matches(
        pack, lexical_plan, plan, error_buf, error_buf_size);
}

bool ppguard_plan_v1_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1ProvenanceInput *provenance,
    PPGuardPlanV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardPlanV1 result;
    PPGuardPlanV1RawDerivation *raw = NULL;
    Atom **answers = NULL;
    size_t entry_len = 0u;
    size_t raw_index;
    size_t entry_index;
    uint32_t evidence_index = 0u;
    bool ok = false;

    ppguard_plan_v1_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !provenance || !out ||
        provenance->derivation_len > UINT32_MAX ||
        (provenance->derivation_len > 0u &&
         !provenance->derivations) ||
        !ppguard_plan_v1_digest_valid(pack->pack_digest) ||
        !ppguard_plan_v1_digest_valid(provenance->source_digest) ||
        !ppguard_plan_v1_digest_valid(
            provenance->pre_reflection_digest) ||
        !ppguard_plan_v1_digest_valid(provenance->environment_digest) ||
        !ppguard_plan_v1_digest_valid(provenance->answer_set_digest) ||
        !ppguard_plan_v1_digest_valid(
            provenance->regular_compiler_digest) ||
        !ppguard_plan_v1_digest_valid(
            provenance->guard_nfa_answer_digest) ||
        provenance->guard_nfa_tag_len > UINT32_MAX ||
        (provenance->guard_nfa_tag_len > 0u &&
         !provenance->guard_nfa_tags) ||
        !ppguard_plan_v1_array_fits(
            provenance->derivation_len, sizeof(*raw))) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "bad positive guard plan or provenance arguments");
        goto done;
    }
    if (lexical_plan) {
        if (!ppguard_plan_v1_lexical_validate(
                pack, lexical_plan, error_buf, error_buf_size)) {
            goto done;
        }
        result.has_lexical_plan = true;
        result.lexical_terminal_len = lexical_plan->entry_len;
        memcpy(result.lexical_plan_digest,
               lexical_plan->plan_digest, 65u);
    }
    if (result.lexical_terminal_len >
            UINT32_MAX - pack->terminal_len) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "positive guard lexical terminal count overflow");
        goto done;
    }
    if (provenance->derivation_len > 0u) {
        raw = calloc(provenance->derivation_len, sizeof(*raw));
        if (!raw)
            goto done;
    }
    for (raw_index = 0u;
         raw_index < provenance->derivation_len; raw_index++) {
        PPGuardPlanV1RawDerivation *derivation = &raw[raw_index];

        derivation->input = &provenance->derivations[raw_index];
        if (!derivation->input->answer ||
            !derivation->input->certificate ||
            !ppguard_plan_v1_answer_fields(
                derivation->input->answer,
                &derivation->owner, &derivation->state,
                &derivation->tag, &derivation->body,
                &derivation->production, &derivation->terminal)) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "malformed positive guard derivation root");
            goto done;
        }
        derivation->state_canonical =
            ppguard_plan_v1_render(derivation->state);
        derivation->answer_canonical =
            ppguard_plan_v1_render(derivation->input->answer);
        derivation->certificate_canonical =
            ppguard_plan_v1_render(derivation->input->certificate);
        derivation->evidence_canonical =
            derivation->answer_canonical &&
                derivation->certificate_canonical
            ? ppguard_plan_v1_evidence_key(
                  derivation->answer_canonical,
                  derivation->certificate_canonical)
            : NULL;
        if (!derivation->state_canonical ||
            !derivation->answer_canonical ||
            !derivation->certificate_canonical ||
            !derivation->evidence_canonical) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard derivation is not ground");
            goto done;
        }
    }
    if (provenance->derivation_len > 1u) {
        qsort(raw, provenance->derivation_len, sizeof(*raw),
              ppguard_plan_v1_raw_compare);
    }
    for (raw_index = 0u;
         raw_index < provenance->derivation_len; raw_index++) {
        if (raw_index > 0u &&
            strcmp(raw[raw_index - 1u].state_canonical,
                   raw[raw_index].state_canonical) == 0) {
            if (strcmp(raw[raw_index - 1u].answer_canonical,
                       raw[raw_index].answer_canonical) != 0 ||
                !atom_eq(raw[raw_index - 1u].input->answer,
                         raw[raw_index].input->answer)) {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "one positive guard state has conflicting artifacts");
                goto done;
            }
            if (strcmp(raw[raw_index - 1u].certificate_canonical,
                       raw[raw_index].certificate_canonical) == 0) {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "duplicate positive guard derivation root");
                goto done;
            }
        } else {
            entry_len++;
        }
    }
    if (entry_len > UINT32_MAX ||
        entry_len > UINT32_MAX - pack->production_len ||
        entry_len > UINT32_MAX - pack->state_len ||
        entry_len > UINT32_MAX - pack->terminal_len -
            result.lexical_terminal_len) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "positive guard plan inventory overflow");
        goto done;
    }
    result.entry_len = (uint32_t)entry_len;
    result.derivation_len = (uint32_t)provenance->derivation_len;
    result.production_len = result.entry_len;
    result.terminal_len = result.lexical_terminal_len + result.entry_len;
    if (result.entry_len > 0u) {
        result.entries = calloc(result.entry_len, sizeof(*result.entries));
        result.states = calloc(result.entry_len, sizeof(*result.states));
        result.productions = calloc(
            result.production_len, sizeof(*result.productions));
        result.production_items = calloc(
            result.production_len, sizeof(*result.production_items));
        result.guard_terminal_ids = malloc(
            sizeof(*result.guard_terminal_ids) * result.entry_len);
        answers = calloc(result.entry_len, sizeof(*answers));
    }
    if (result.derivation_len > 0u) {
        result.derivations = calloc(
            result.derivation_len, sizeof(*result.derivations));
    }
    if (result.terminal_len > 0u) {
        result.terminals = calloc(
            result.terminal_len, sizeof(*result.terminals));
    }
    if ((result.entry_len > 0u &&
         (!result.entries || !result.states || !result.productions ||
          !result.production_items || !result.guard_terminal_ids ||
          !answers)) ||
        (result.derivation_len > 0u && !result.derivations) ||
        (result.terminal_len > 0u && !result.terminals)) {
        goto done;
    }
    for (entry_index = 0u;
         entry_index < result.lexical_terminal_len; entry_index++) {
        result.terminals[entry_index].terminal_id =
            pack->terminal_len + (uint32_t)entry_index;
        result.terminals[entry_index].identity = atom_deep_copy(
            &result.arena,
            lexical_plan->terminals[entry_index].identity);
        if (!result.terminals[entry_index].identity)
            goto done;
    }
    raw_index = 0u;
    for (entry_index = 0u; entry_index < entry_len; entry_index++) {
        PPGuardPlanV1Entry *entry = &result.entries[entry_index];
        PPNativeV1TerminalExtension *terminal =
            &result.terminals[result.lexical_terminal_len + entry_index];
        PPNativeV1ProductionExtension *production =
            &result.productions[entry_index];
        size_t group_end = raw_index + 1u;
        int32_t base_state;
        size_t group_index;

        while (group_end < provenance->derivation_len &&
               strcmp(raw[raw_index].state_canonical,
                      raw[group_end].state_canonical) == 0) {
            group_end++;
        }
        entry->owner = atom_deep_copy(&result.arena, raw[raw_index].owner);
        entry->state = atom_deep_copy(&result.arena, raw[raw_index].state);
        entry->tag = atom_deep_copy(&result.arena, raw[raw_index].tag);
        entry->body = atom_deep_copy(&result.arena, raw[raw_index].body);
        entry->production = atom_deep_copy(
            &result.arena, raw[raw_index].production);
        entry->state_canonical = ppguard_plan_v1_text_dup(
            raw[raw_index].state_canonical);
        entry->terminal_id = pack->terminal_len +
            result.lexical_terminal_len + (uint32_t)entry_index;
        entry->production_id =
            pack->production_len + (uint32_t)entry_index;
        entry->evidence_begin = evidence_index;
        entry->evidence_len = (uint32_t)(group_end - raw_index);
        if (!entry->owner || !entry->state || !entry->tag ||
            !entry->body || !entry->production ||
            !entry->state_canonical) {
            goto done;
        }
        base_state = ppnative_v1_state_find(pack, entry->state);
        if (base_state >= 0) {
            if (pack->states[base_state].defined) {
                ppguard_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard cannot replace a defined ParserPack state");
                goto done;
            }
            entry->state_id = (uint32_t)base_state;
        } else {
            PPNativeV1StateExtension *state =
                &result.states[result.state_len];
            entry->state_is_extension = true;
            entry->state_id = pack->state_len + result.state_len;
            state->state_id = entry->state_id;
            state->identity = entry->state;
            result.state_len++;
        }
        terminal->terminal_id = entry->terminal_id;
        terminal->identity = atom_deep_copy(
            &result.arena, raw[raw_index].terminal);
        result.guard_terminal_ids[entry_index] = entry->terminal_id;
        result.production_items[entry_index] = (PPABIV1Item){
            .kind = PPABI_V1_ITEM_TERMINAL,
            .dense_id = entry->terminal_id,
        };
        production->production_id = entry->production_id;
        production->identity = entry->production;
        production->lhs_state_id = entry->state_id;
        production->items = &result.production_items[entry_index];
        production->item_len = 1u;
        production->action = entry->production->expr.elems[4];
        if (!terminal->identity)
            goto done;
        for (group_index = raw_index;
             group_index < group_end; group_index++) {
            PPGuardPlanV1Derivation *derivation =
                &result.derivations[evidence_index];
            derivation->entry_index = (uint32_t)entry_index;
            derivation->answer = atom_deep_copy(
                &result.arena, raw[group_index].input->answer);
            derivation->certificate = atom_deep_copy(
                &result.arena, raw[group_index].input->certificate);
            derivation->canonical = ppguard_plan_v1_text_dup(
                raw[group_index].evidence_canonical);
            if (!derivation->answer || !derivation->certificate ||
                !derivation->canonical) {
                goto done;
            }
            evidence_index++;
        }
        answers[entry_index] =
            result.derivations[entry->evidence_begin].answer;
        raw_index = group_end;
    }
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.source_digest, provenance->source_digest, 65u);
    memcpy(result.pre_reflection_digest,
           provenance->pre_reflection_digest, 65u);
    memcpy(result.environment_digest,
           provenance->environment_digest, 65u);
    memcpy(result.answer_set_digest,
           provenance->answer_set_digest, 65u);
    memcpy(result.regular_compiler_digest,
           provenance->regular_compiler_digest, 65u);
    memcpy(result.guard_nfa_answer_digest,
           provenance->guard_nfa_answer_digest, 65u);
    if (!ppguard_plan_v1_guard_tags_bijective(
            &result, provenance->guard_nfa_tags,
            provenance->guard_nfa_tag_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (!ppguard_plan_v1_answer_set_digest(
            answers, result.entry_len, result.plan_digest) ||
        strcmp(result.plan_digest, result.answer_set_digest) != 0) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "positive guard answer-set digest does not match provenance");
        goto done;
    }
    if (!ppguard_plan_v1_compute_digest(&result, result.plan_digest) ||
        !ppguard_plan_v1_compute_evidence_digest(
            &result, result.evidence_digest) ||
        !ppguard_plan_v1_matches(
            pack, lexical_plan, &result,
            error_buf, error_buf_size)) {
        goto done;
    }
    ppguard_plan_v1_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(answers);
    ppguard_plan_v1_raw_free(raw, provenance ? provenance->derivation_len : 0u);
    ppguard_plan_v1_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "failed to build positive guard plan");
    }
    return ok;
}

bool ppguard_plan_v1_grammar_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    CettaLpNativeGrammar *grammar,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1ForestExtension extension;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !ppguard_plan_v1_matches(
            pack, lexical_plan, plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "bad positive guard grammar inputs");
        }
        return false;
    }
    if (lexical_plan) {
        if (!pplex_v1_grammar_build(
                pack, lexical_plan, grammar,
                error_buf, error_buf_size)) {
            return false;
        }
    } else if (!ppnative_v1_grammar_build(pack, grammar)) {
        ppguard_plan_v1_set_error(
            error_buf, error_buf_size,
            "failed to copy positive guard base grammar");
        return false;
    }
    extension = (PPNativeV1ForestExtension){
        .states = plan->states,
        .state_len = plan->state_len,
        .terminals = plan->terminals,
        .terminal_len = plan->terminal_len,
        .productions = plan->productions,
        .production_len = plan->production_len,
    };
    if (!ppnative_v1_grammar_extend(
            pack, &extension, grammar, error_buf, error_buf_size)) {
        cetta_lp_native_grammar_free(grammar);
        return false;
    }
    return true;
}

static bool ppguard_plan_v1_lattice_has_terminal(
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t terminal_id) {
    uint32_t low = 0u;
    uint32_t high = lattice->terminal_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (lattice->terminal_ids[middle] < terminal_id)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < lattice->terminal_len &&
        lattice->terminal_ids[low] == terminal_id;
}

static bool ppguard_plan_v1_receipt_digest(
    PPGuardPlanV1Backend backend,
    PPGuardPlanV1Receipt *receipt) {
    static const char domain[] = "ParserPackPositiveGuardExecutionV1";
    CettaNativeSha256 sha;

    cetta_native_sha256_init(&sha);
    ppguard_plan_v1_sha_bytes(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppguard_plan_v1_sha_u32(&sha, (uint32_t)backend);
    ppguard_plan_v1_sha_text(&sha, receipt->plan_digest);
    ppguard_plan_v1_sha_text(&sha, receipt->evidence_digest);
    ppguard_plan_v1_sha_text(&sha, receipt->relation_digest);
    ppguard_plan_v1_sha_text(&sha, receipt->forest_digest);
    ppguard_plan_v1_sha_u32(&sha, receipt->source_pass_count);
    ppguard_plan_v1_sha_u32(&sha, receipt->work_item_len);
    ppguard_plan_v1_sha_u32(&sha, (uint32_t)receipt->outcome);
    ppguard_plan_v1_sha_u32(&sha, receipt->accepted ? 1u : 0u);
    cetta_native_sha256_finish_hex(&sha, receipt->execution_digest);
    return true;
}

static bool ppguard_plan_v1_parse(
    PPGuardPlanV1Backend backend,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    const CettaLpNativeGllPrepared *prepared_gll,
    const CettaLpNativeGlrPrepared *prepared_glr,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result result;
    PPGuardPlanV1Receipt execution = {0};
    PPNativeV1ForestExtension extension;
    CettaLpNativeGrammar grammar;
    int32_t start_id;
    uint32_t index;
    bool ok = false;

    ppnative_v1_result_init(&result);
    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!start_state || !relation || !out || !receipt ||
        work_limit == 0u || replay_depth == 0u || result_limit == 0u ||
        (prepared_gll && backend != PPGUARD_PLAN_V1_BACKEND_GLL) ||
        (prepared_glr && backend != PPGUARD_PLAN_V1_BACKEND_GLR) ||
        !ppguard_plan_v1_matches(
            pack, lexical_plan, plan, error_buf, error_buf_size) ||
        !ppguard_relation_v1_validate(
            relation, error_buf, error_buf_size) ||
        strcmp(relation->source_digest, plan->plan_digest) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "bad positive guard parse inputs or provenance");
        }
        goto done;
    }
    for (index = 0u; index < plan->entry_len; index++) {
        if (!ppguard_plan_v1_lattice_has_terminal(
                &relation->lattice, plan->guard_terminal_ids[index])) {
            ppguard_plan_v1_set_error(
                error_buf, error_buf_size,
                "positive guard relation omits a planned terminal");
            goto done;
        }
    }
    extension = (PPNativeV1ForestExtension){
        .states = plan->states,
        .state_len = plan->state_len,
        .terminals = plan->terminals,
        .terminal_len = plan->terminal_len,
        .productions = plan->productions,
        .production_len = plan->production_len,
        .witness_values = relation->witness_values,
        .witness_len = relation->witness_len,
    };
    start_id = ppnative_v1_state_find_extended(
        pack, &extension, start_state);
    if (start_id < 0 || !ppnative_v1_start_is_closed_extended(
                            pack, start_state, &extension,
                            result.detail, sizeof(result.detail))) {
        result.outcome = PPNATIVE_V1_UNSUPPORTED_OPEN_PACK;
        if (result.detail[0] == '\0') {
            (void)snprintf(result.detail, sizeof(result.detail),
                           "start state is absent");
        }
        goto finish;
    }
    if (!prepared_gll && !prepared_glr) {
        if (!ppguard_plan_v1_grammar_build(
                pack, lexical_plan, plan, &grammar,
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    if (backend == PPGUARD_PLAN_V1_BACKEND_GLL) {
        bool parsed = prepared_gll
            ? cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
                  prepared_gll, &relation->lattice, 0u,
                  work_limit, &result.forest,
                  error_buf, error_buf_size)
            : cetta_lp_native_gll_parse_utf8_lattice_forest_complete(
                  &grammar, (uint32_t)start_id, &relation->lattice,
                  work_limit, &result.forest,
                  error_buf, error_buf_size);
        if (!parsed) {
            goto done;
        }
    } else {
        bool parsed = prepared_glr
            ? cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
                  prepared_glr, &relation->lattice, 0u,
                  work_limit, &result.forest,
                  error_buf, error_buf_size)
            : cetta_lp_native_glr_parse_utf8_lattice_forest(
                  &grammar, (uint32_t)start_id, &relation->lattice,
                  work_limit, &result.forest,
                  error_buf, error_buf_size);
        if (!parsed)
            goto done;
    }
    if (result.forest.outcome ==
        CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        result.outcome = PPNATIVE_V1_RECOGNIZER_LIMIT;
        (void)snprintf(
            result.detail, sizeof(result.detail), "%s work limit %u",
            backend == PPGUARD_PLAN_V1_BACKEND_GLL ? "GLL" : "GLR",
            work_limit);
        goto finish;
    }
    for (index = 0u; index < result.forest.root_len; index++) {
        uint32_t node_index = result.forest.roots[index];
        if (node_index < result.forest.node_len &&
            result.forest.nodes[node_index].scalar_left == 0u &&
            result.forest.nodes[node_index].scalar_right ==
                result.forest.scalar_len) {
            result.accepted = true;
            break;
        }
    }
    if (!ppnative_v1_finish_extended(
            &result, pack, start_state, &extension,
            replay_depth, result_limit,
            error_buf, error_buf_size)) {
        goto done;
    }

finish:
    memcpy(execution.plan_digest, plan->plan_digest, 65u);
    memcpy(execution.evidence_digest, plan->evidence_digest, 65u);
    memcpy(execution.relation_digest, relation->relation_digest, 65u);
    if (result.forest_digest[0] != '\0')
        memcpy(execution.forest_digest, result.forest_digest, 65u);
    execution.source_pass_count = relation->lattice.source_pass_count;
    execution.work_item_len = result.forest.work_item_len;
    execution.outcome = result.outcome;
    execution.accepted = result.accepted;
    (void)ppguard_plan_v1_receipt_digest(backend, &execution);
    ppnative_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    *receipt = execution;
    ok = true;

done:
    cetta_lp_native_grammar_free(&grammar);
    ppnative_v1_result_free(&result);
    return ok;
}

bool ppguard_plan_v1_gll_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_plan_v1_parse(
        PPGUARD_PLAN_V1_BACKEND_GLL,
        pack, start_state, lexical_plan, plan, relation,
        NULL, NULL,
        descriptor_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}

bool ppguard_plan_v1_glr_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_plan_v1_parse(
        PPGUARD_PLAN_V1_BACKEND_GLR,
        pack, start_state, lexical_plan, plan, relation,
        NULL, NULL,
        work_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}

bool ppguard_plan_v1_gll_parse_prepared(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    const CettaLpNativeGllPrepared *prepared,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_plan_v1_parse(
        PPGUARD_PLAN_V1_BACKEND_GLL,
        pack, start_state, lexical_plan, plan, relation,
        prepared, NULL,
        descriptor_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}

bool ppguard_plan_v1_glr_parse_prepared(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *plan,
    const PPGuardRelationV1 *relation,
    const CettaLpNativeGlrPrepared *prepared,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    PPGuardPlanV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_plan_v1_parse(
        PPGUARD_PLAN_V1_BACKEND_GLR,
        pack, start_state, lexical_plan, plan, relation,
        NULL, prepared,
        work_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}
