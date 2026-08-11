#include "parser_pack_guarded_lexical_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include "symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ppguarded_lex_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppguarded_lex_v1_expr_head(
    const Atom *term, const char *head, CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == argument_len + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static bool ppguarded_lex_v1_strict_descendant(
    const Atom *state, const Atom *owner, size_t max_depth) {
    const Atom *cursor = state;
    size_t depth = 0u;

    while (ppguarded_lex_v1_expr_head(cursor, "pp-sub", 2u)) {
        if (depth >= max_depth)
            return false;
        depth++;
        cursor = cursor->expr.elems[1];
    }
    return depth > 0u && atom_eq((Atom *)cursor, (Atom *)owner);
}

static bool ppguarded_lex_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static char *ppguarded_lex_v1_canonical(const Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u)) {
        free(bytes);
        return NULL;
    }
    return (char *)bytes;
}

static char *ppguarded_lex_v1_text_dup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static Atom *ppguarded_lex_v1_unary(
    Arena *arena, const char *head, Atom *argument) {
    Atom *items[2];

    items[0] = atom_symbol(arena, head);
    items[1] = argument;
    if (!items[0] || !items[1])
        return NULL;
    return atom_expr(arena, items, 2u);
}

static void ppguarded_lex_v1_sha_text(
    CettaNativeSha256 *sha, const char *text) {
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, strlen(text));
}

static void ppguarded_lex_v1_sha_u32(
    CettaNativeSha256 *sha, uint32_t value) {
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppguarded_lex_v1_digest(
    const PPGuardedLexV1Plan *plan, char out[65]) {
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppguarded_lex_v1_sha_text(
        &sha, "ParserPackGuardedLexicalProjectionV1\n");
    ppguarded_lex_v1_sha_text(&sha, plan->base_pack_digest);
    ppguarded_lex_v1_sha_text(&sha, "\n");
    ppguarded_lex_v1_sha_text(&sha, plan->lexical_plan_digest);
    ppguarded_lex_v1_sha_text(&sha, "\n");
    ppguarded_lex_v1_sha_text(&sha, plan->guard_plan_digest);
    ppguarded_lex_v1_sha_text(&sha, "\n");
    ppguarded_lex_v1_sha_text(&sha, plan->guarded_compiler_digest);
    ppguarded_lex_v1_sha_text(&sha, "\n");
    ppguarded_lex_v1_sha_text(&sha, plan->guarded_nfa_answer_digest);
    ppguarded_lex_v1_sha_text(&sha, "\n");
    for (index = 0u; index < plan->entry_len; index++) {
        const PPGuardedLexV1Entry *entry = &plan->entries[index];
        ppguarded_lex_v1_sha_u32(&sha, entry->tag_index);
        ppguarded_lex_v1_sha_u32(&sha, entry->state_id);
        ppguarded_lex_v1_sha_u32(&sha, entry->terminal_index);
        ppguarded_lex_v1_sha_u32(&sha, entry->guard_entry_index);
        ppguarded_lex_v1_sha_text(&sha, entry->tag_canonical);
        ppguarded_lex_v1_sha_text(&sha, "\n");
        ppguarded_lex_v1_sha_text(&sha, entry->state_canonical);
        ppguarded_lex_v1_sha_text(&sha, "\n");
        ppguarded_lex_v1_sha_text(&sha, entry->guard_state_canonical);
        ppguarded_lex_v1_sha_text(&sha, "\n");
        ppguarded_lex_v1_sha_text(&sha, entry->guard_body_canonical);
        ppguarded_lex_v1_sha_text(&sha, "\n");
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

void ppguarded_lex_v1_plan_init(PPGuardedLexV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    arena_init(&plan->arena);
    rsnfa_v1_plan_init(&plan->prefix_nfa);
}

void ppguarded_lex_v1_plan_free(PPGuardedLexV1Plan *plan) {
    uint32_t index;

    if (!plan)
        return;
    for (index = 0u; index < plan->entry_len; index++) {
        free(plan->entries[index].tag_canonical);
        free(plan->entries[index].state_canonical);
        free(plan->entries[index].guard_state_canonical);
        free(plan->entries[index].guard_body_canonical);
    }
    free(plan->entries);
    free(plan->terminals);
    free(plan->tag_terminal_ids);
    rsnfa_v1_plan_free(&plan->prefix_nfa);
    arena_free(&plan->arena);
    memset(plan, 0, sizeof(*plan));
}

static int32_t ppguarded_lex_v1_pack_state(
    const PPABIV1Pack *pack, const Atom *tag) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < pack->state_len; index++) {
        const Atom *identity = pack->states[index].identity;
        if (!ppguarded_lex_v1_expr_head(identity, "pp-def", 1u) ||
            !atom_eq(identity->expr.elems[1], (Atom *)tag)) {
            continue;
        }
        if (found >= 0)
            return -1;
        found = (int32_t)index;
    }
    return found;
}

static int32_t ppguarded_lex_v1_guard_entry(
    const PPGuardPlanV1 *plan,
    const Atom *owner,
    const Atom *state,
    const Atom *body) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < plan->entry_len; index++) {
        const PPGuardPlanV1Entry *entry = &plan->entries[index];
        if (!atom_eq(entry->owner, (Atom *)owner) ||
            !atom_eq(entry->state, (Atom *)state) ||
            !atom_eq(entry->body, (Atom *)body)) {
            continue;
        }
        if (found >= 0)
            return -1;
        found = (int32_t)index;
    }
    return found;
}

static int ppguarded_lex_v1_entry_compare(
    const void *left, const void *right) {
    const PPGuardedLexV1Entry *lhs = left;
    const PPGuardedLexV1Entry *rhs = right;
    return strcmp(lhs->state_canonical, rhs->state_canonical);
}

static bool ppguarded_lex_v1_metadata(
    Atom *const *answers,
    size_t answer_len,
    const Atom *tag,
    Atom **state,
    Atom **body,
    char *error_buf,
    size_t error_buf_size) {
    size_t index;
    bool found = false;

    *state = NULL;
    *body = NULL;
    for (index = 0u; index < answer_len; index++) {
        Atom *answer = answers[index];
        if (!ppguarded_lex_v1_expr_head(
                answer, "compile-guarded-span-nfa", 4u)) {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "unknown guarded-span NFA answer");
            return false;
        }
        if (!atom_eq(answer->expr.elems[1], (Atom *)tag))
            continue;
        if (!found) {
            *state = answer->expr.elems[2];
            *body = answer->expr.elems[3];
            found = true;
        } else if (!atom_eq(*state, answer->expr.elems[2]) ||
                   !atom_eq(*body, answer->expr.elems[3])) {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "guarded-span NFA metadata is inconsistent");
            return false;
        }
    }
    if (!found) {
        ppguarded_lex_v1_set_error(
            error_buf, error_buf_size,
            "guarded-span NFA tag has no metadata");
    }
    return found;
}

static bool ppguarded_lex_v1_plan_matches(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *plan) {
    char digest[65];
    uint32_t index;

    if (!pack || !lexical_plan || !guard_plan || !plan ||
        plan->entry_len != plan->prefix_nfa.nfa.tag_len ||
        plan->entry_len != guard_plan->entry_len ||
        (plan->entry_len > 0u &&
         (!plan->entries || !plan->terminals ||
          !plan->tag_terminal_ids)) ||
        !ppguarded_lex_v1_digest_valid(plan->base_pack_digest) ||
        !ppguarded_lex_v1_digest_valid(plan->lexical_plan_digest) ||
        !ppguarded_lex_v1_digest_valid(plan->guard_plan_digest) ||
        !ppguarded_lex_v1_digest_valid(plan->guarded_compiler_digest) ||
        !ppguarded_lex_v1_digest_valid(plan->guarded_nfa_answer_digest) ||
        !ppguarded_lex_v1_digest_valid(plan->plan_digest) ||
        strcmp(pack->pack_digest, plan->base_pack_digest) != 0 ||
        strcmp(lexical_plan->plan_digest,
               plan->lexical_plan_digest) != 0 ||
        strcmp(guard_plan->plan_digest, plan->guard_plan_digest) != 0 ||
        guard_plan->terminal_len > UINT32_MAX - pack->terminal_len ||
        plan->entry_len > UINT32_MAX - pack->terminal_len -
            guard_plan->terminal_len) {
        return false;
    }
    for (index = 0u; index < plan->entry_len; index++) {
        const PPGuardedLexV1Entry *entry = &plan->entries[index];
        const PPNativeV1TerminalExtension *terminal =
            &plan->terminals[index];
        if (entry->tag_index >= plan->entry_len ||
            entry->state_id >= pack->state_len ||
            entry->terminal_index != index ||
            entry->guard_entry_index >= guard_plan->entry_len ||
            !entry->tag || !entry->guard_state || !entry->guard_body ||
            !entry->tag_canonical || !entry->state_canonical ||
            !entry->guard_state_canonical ||
            !entry->guard_body_canonical ||
            !atom_eq(entry->tag,
                     plan->prefix_nfa.tags[entry->tag_index]) ||
            strcmp(entry->tag_canonical,
                   plan->prefix_nfa.tag_canonical[entry->tag_index]) != 0 ||
            !ppguarded_lex_v1_expr_head(
                pack->states[entry->state_id].identity, "pp-def", 1u) ||
            !atom_eq(pack->states[entry->state_id].identity->expr.elems[1],
                     entry->tag) ||
            strcmp(pack->states[entry->state_id].canonical,
                   entry->state_canonical) != 0 ||
            !atom_eq(entry->tag,
                     guard_plan->entries[entry->guard_entry_index].owner) ||
            !atom_eq(entry->guard_state,
                     guard_plan->entries[entry->guard_entry_index].state) ||
            !atom_eq(entry->guard_body,
                     guard_plan->entries[entry->guard_entry_index].body) ||
            !ppguarded_lex_v1_expr_head(
                entry->guard_state, "pp-sub", 2u) ||
            !ppguarded_lex_v1_strict_descendant(
                entry->guard_state,
                pack->states[entry->state_id].identity,
                (size_t)pack->state_len + guard_plan->state_len + 1u) ||
            !atom_is_symbol(entry->guard_state->expr.elems[2], "right") ||
            terminal->terminal_id != pack->terminal_len +
                guard_plan->terminal_len + index ||
            !ppguarded_lex_v1_expr_head(
                terminal->identity, "pp-guarded-span-terminal", 1u) ||
            !atom_eq(terminal->identity->expr.elems[1], entry->tag) ||
            plan->tag_terminal_ids[entry->tag_index] !=
                terminal->terminal_id ||
            (index > 0u &&
             strcmp(plan->entries[index - 1u].state_canonical,
                    entry->state_canonical) >= 0)) {
            return false;
        }
        for (uint32_t prior = 0u; prior < index; prior++) {
            if (plan->entries[prior].guard_entry_index ==
                entry->guard_entry_index) {
                return false;
            }
        }
        for (uint32_t lexical = 0u;
             lexical < lexical_plan->entry_len; lexical++) {
            if (lexical_plan->entries[lexical].state_id ==
                entry->state_id) {
                return false;
            }
        }
    }
    ppguarded_lex_v1_digest(plan, digest);
    return strcmp(digest, plan->plan_digest) == 0;
}

bool ppguarded_lex_v1_plan_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    Atom *const *guarded_nfa_answers,
    size_t guarded_nfa_answer_len,
    const char *guarded_compiler_digest,
    const char *guarded_nfa_answer_digest,
    PPGuardedLexV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexV1Plan result;
    CettaLpNativeGrammar validation_grammar;
    Atom **normal_answers = NULL;
    PPNativeV1ForestExtension extension;
    size_t answer_index;
    uint32_t index;
    bool ok = false;

    ppguarded_lex_v1_plan_init(&result);
    cetta_lp_native_grammar_init(&validation_grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !lexical_plan || !guard_plan || !out ||
        (guarded_nfa_answer_len > 0u && !guarded_nfa_answers) ||
        guarded_nfa_answer_len > UINT32_MAX ||
        !ppguarded_lex_v1_digest_valid(guarded_compiler_digest) ||
        !ppguarded_lex_v1_digest_valid(guarded_nfa_answer_digest) ||
        !ppguard_plan_v1_grammar_build(
            pack, lexical_plan, guard_plan,
            &validation_grammar, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "bad guarded lexical projection inputs");
        }
        goto done;
    }
    if (guarded_nfa_answer_len > 0u) {
        normal_answers = calloc(
            guarded_nfa_answer_len, sizeof(*normal_answers));
        if (!normal_answers)
            goto done;
    }
    for (answer_index = 0u;
         answer_index < guarded_nfa_answer_len; answer_index++) {
        Atom *answer = guarded_nfa_answers[answer_index];
        Atom *head;
        Atom *tag;
        Atom *edge;
        if (!ppguarded_lex_v1_expr_head(
                answer, "compile-guarded-span-nfa", 4u) ||
            !(head = atom_symbol(&result.arena, "compile-span-nfa")) ||
            !(tag = atom_deep_copy(
                &result.arena, answer->expr.elems[1])) ||
            !(edge = atom_deep_copy(
                &result.arena, answer->expr.elems[4])) ||
            !(normal_answers[answer_index] = atom_expr3(
                &result.arena, head, tag, edge))) {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "malformed guarded-span NFA answer");
            goto done;
        }
    }
    if (guarded_nfa_answer_len > 0u) {
        if (!rsnfa_v1_plan_load(
                pack, normal_answers, guarded_nfa_answer_len,
                &result.prefix_nfa, error_buf, error_buf_size)) {
            goto done;
        }
    }
    if (result.prefix_nfa.nfa.tag_len != guard_plan->entry_len) {
        ppguarded_lex_v1_set_error(
            error_buf, error_buf_size,
            "guarded lexical projection does not cover every guard");
        goto done;
    }
    result.entry_len = result.prefix_nfa.nfa.tag_len;
    if (result.entry_len > 0u) {
        result.entries = calloc(result.entry_len, sizeof(*result.entries));
        result.terminals = calloc(
            result.entry_len, sizeof(*result.terminals));
        result.tag_terminal_ids = malloc(
            sizeof(*result.tag_terminal_ids) * (size_t)result.entry_len);
        if (!result.entries || !result.terminals ||
            !result.tag_terminal_ids) {
            goto done;
        }
    }
    extension = (PPNativeV1ForestExtension){
        .states = guard_plan->states,
        .state_len = guard_plan->state_len,
        .terminals = guard_plan->terminals,
        .terminal_len = guard_plan->terminal_len,
        .productions = guard_plan->productions,
        .production_len = guard_plan->production_len,
    };
    for (index = 0u; index < result.entry_len; index++) {
        PPGuardedLexV1Entry *entry = &result.entries[index];
        Atom *tag = result.prefix_nfa.tags[index];
        Atom *guard_state = NULL;
        Atom *guard_body = NULL;
        Atom *owner;
        int32_t state_id;
        int32_t guard_entry_index;
        char closure[512] = {0};
        if (!ppguarded_lex_v1_metadata(
                guarded_nfa_answers, guarded_nfa_answer_len,
                tag, &guard_state, &guard_body,
                error_buf, error_buf_size) ||
            (state_id = ppguarded_lex_v1_pack_state(pack, tag)) < 0) {
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
                ppguarded_lex_v1_set_error(
                    error_buf, error_buf_size,
                    "guarded lexical tag has no unique definition state");
            }
            goto done;
        }
        owner = pack->states[state_id].identity;
        if (!ppguarded_lex_v1_expr_head(
                guard_state, "pp-sub", 2u) ||
            !ppguarded_lex_v1_strict_descendant(
                guard_state, owner,
                (size_t)pack->state_len + guard_plan->state_len + 1u) ||
            !atom_is_symbol(guard_state->expr.elems[2], "right") ||
            (guard_entry_index = ppguarded_lex_v1_guard_entry(
                guard_plan, tag, guard_state, guard_body)) < 0 ||
            !ppnative_v1_start_is_closed_extended(
                pack, owner, &extension, closure, sizeof(closure))) {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical metadata is not an exact trailing guard%s%s",
                closure[0] ? ": " : "", closure);
            goto done;
        }
        entry->tag_index = index;
        entry->state_id = (uint32_t)state_id;
        entry->guard_entry_index = (uint32_t)guard_entry_index;
        entry->tag = atom_deep_copy(&result.arena, tag);
        entry->guard_state = atom_deep_copy(
            &result.arena, guard_state);
        entry->guard_body = atom_deep_copy(&result.arena, guard_body);
        entry->tag_canonical = ppguarded_lex_v1_canonical(tag);
        entry->state_canonical = ppguarded_lex_v1_text_dup(
            pack->states[state_id].canonical);
        entry->guard_state_canonical =
            ppguarded_lex_v1_canonical(guard_state);
        entry->guard_body_canonical =
            ppguarded_lex_v1_canonical(guard_body);
        if (!entry->tag || !entry->guard_state || !entry->guard_body ||
            !entry->tag_canonical ||
            !entry->state_canonical || !entry->guard_state_canonical ||
            !entry->guard_body_canonical) {
            goto done;
        }
    }
    if (result.entry_len > 1u) {
        qsort(result.entries, result.entry_len,
              sizeof(*result.entries),
              ppguarded_lex_v1_entry_compare);
    }
    for (index = 0u; index < result.entry_len; index++) {
        PPGuardedLexV1Entry *entry = &result.entries[index];
        uint32_t terminal_id;
        if (index > 0u &&
            strcmp(result.entries[index - 1u].state_canonical,
                   entry->state_canonical) == 0) {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical definition is duplicated");
            goto done;
        }
        if (pack->terminal_len > UINT32_MAX - guard_plan->terminal_len ||
            index > UINT32_MAX - pack->terminal_len -
                guard_plan->terminal_len) {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical terminal count overflow");
            goto done;
        }
        terminal_id = pack->terminal_len + guard_plan->terminal_len + index;
        entry->terminal_index = index;
        result.terminals[index].terminal_id = terminal_id;
        result.terminals[index].identity = ppguarded_lex_v1_unary(
            &result.arena, "pp-guarded-span-terminal",
            atom_deep_copy(&result.arena, entry->tag));
        result.tag_terminal_ids[entry->tag_index] = terminal_id;
        if (!result.terminals[index].identity)
            goto done;
    }
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.lexical_plan_digest,
           lexical_plan->plan_digest, 65u);
    memcpy(result.guard_plan_digest, guard_plan->plan_digest, 65u);
    memcpy(result.guarded_compiler_digest,
           guarded_compiler_digest, 65u);
    memcpy(result.guarded_nfa_answer_digest,
           guarded_nfa_answer_digest, 65u);
    ppguarded_lex_v1_digest(&result, result.plan_digest);
    if (!ppguarded_lex_v1_plan_matches(
            pack, lexical_plan, guard_plan, &result)) {
        ppguarded_lex_v1_set_error(
            error_buf, error_buf_size,
            "guarded lexical projection failed self-validation");
        goto done;
    }
    ppguarded_lex_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(normal_answers);
    cetta_lp_native_grammar_free(&validation_grammar);
    ppguarded_lex_v1_plan_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_v1_set_error(
            error_buf, error_buf_size,
            "failed to build guarded lexical projection");
    }
    return ok;
}

bool ppguarded_lex_v1_grammar_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *plan,
    CettaLpNativeGrammar *grammar,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t production_index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !ppguarded_lex_v1_plan_matches(
            pack, lexical_plan, guard_plan, plan) ||
        !ppguard_plan_v1_grammar_build(
            pack, lexical_plan, guard_plan,
            grammar, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_v1_set_error(
                error_buf, error_buf_size,
                "bad guarded lexical grammar inputs");
        }
        return false;
    }
    for (production_index = 0u;
         production_index < grammar->production_len; production_index++) {
        CettaLpNativeProduction *production =
            &grammar->productions[production_index];
        uint32_t item_index;
        for (item_index = 0u; item_index < production->rhs_len;
             item_index++) {
            CettaLpNativeSymbol *item = &production->rhs[item_index];
            uint32_t entry_index;
            if (item->kind != CETTA_LP_NATIVE_SYMBOL_HL)
                continue;
            for (entry_index = 0u;
                 entry_index < plan->entry_len; entry_index++) {
                if (item->name != plan->entries[entry_index].state_id)
                    continue;
                item->kind = CETTA_LP_NATIVE_SYMBOL_TM;
                item->name = plan->terminals[
                    plan->entries[entry_index].terminal_index].terminal_id;
                item->scope = 0u;
                break;
            }
        }
    }
    return true;
}
