#include "parser_pack_lexical_v1.h"

#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PPLEX_V1_BACKEND_GLL = 0,
    PPLEX_V1_BACKEND_GLR = 1
} PPLexV1Backend;

static void pplex_v1_set_error(char *buf,
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

static bool pplex_v1_digest_valid(const char *digest) {
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

static bool pplex_v1_expr_head(const Atom *atom,
                               const char *head,
                               CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static Atom *pplex_v1_unary(Arena *arena,
                            const char *head,
                            Atom *argument) {
    Atom *items[2];
    items[0] = atom_symbol(arena, head);
    items[1] = argument;
    return atom_expr(arena, items, 2u);
}

static char *pplex_v1_text_dup(const char *text) {
    size_t len = strlen(text);
    char *copy = malloc(len + 1u);
    if (copy)
        memcpy(copy, text, len + 1u);
    return copy;
}

static int pplex_v1_entry_compare(const void *left, const void *right) {
    const PPLexV1Entry *lhs = left;
    const PPLexV1Entry *rhs = right;
    return strcmp(lhs->state_canonical, rhs->state_canonical);
}

static void pplex_v1_sha_text(CettaNativeSha256 *sha, const char *text) {
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, strlen(text));
}

static void pplex_v1_sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void pplex_v1_plan_digest(const PPLexV1Plan *plan, char out[65]) {
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    pplex_v1_sha_text(&sha, "ParserPackLexicalProjectionV1\n");
    pplex_v1_sha_text(&sha, plan->base_pack_digest);
    pplex_v1_sha_text(&sha, "\n");
    pplex_v1_sha_text(&sha, plan->regular_compiler_digest);
    pplex_v1_sha_text(&sha, "\n");
    pplex_v1_sha_text(&sha, plan->nfa_answer_digest);
    pplex_v1_sha_text(&sha, "\n");
    for (index = 0u; index < plan->entry_len; index++) {
        pplex_v1_sha_u32(&sha, plan->entries[index].tag_index);
        pplex_v1_sha_text(&sha, plan->entries[index].state_canonical);
        pplex_v1_sha_text(&sha, "\n");
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

void pplex_v1_plan_init(PPLexV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    arena_init(&plan->arena);
}

void pplex_v1_plan_free(PPLexV1Plan *plan) {
    uint32_t index;
    if (!plan)
        return;
    for (index = 0u; index < plan->entry_len; index++)
        free(plan->entries[index].state_canonical);
    free(plan->entries);
    free(plan->terminals);
    free(plan->tag_terminal_ids);
    arena_free(&plan->arena);
    memset(plan, 0, sizeof(*plan));
}

static int32_t pplex_v1_tag_state_find(const PPABIV1Pack *pack,
                                       Atom *tag) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < pack->state_len; index++) {
        const Atom *identity = pack->states[index].identity;
        if (!pplex_v1_expr_head(identity, "pp-def", 1u) ||
            !atom_eq(identity->expr.elems[1], tag)) {
            continue;
        }
        if (found >= 0)
            return -1;
        found = (int32_t)index;
    }
    return found;
}

bool pplex_v1_plan_build(
    const PPABIV1Pack *pack,
    Atom *const *tags,
    uint32_t tag_len,
    const char *regular_compiler_digest,
    const char *nfa_answer_digest,
    PPLexV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPLexV1Plan result;
    uint32_t index;
    bool ok = false;

    pplex_v1_plan_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !out || tag_len == 0u || !tags ||
        !pplex_v1_digest_valid(pack->pack_digest) ||
        !pplex_v1_digest_valid(regular_compiler_digest) ||
        !pplex_v1_digest_valid(nfa_answer_digest) ||
        pack->terminal_len == UINT32_MAX ||
        tag_len > UINT32_MAX - pack->terminal_len) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "bad lexical projection inputs");
        goto done;
    }
    result.entries = calloc(tag_len, sizeof(*result.entries));
    result.terminals = calloc(tag_len, sizeof(*result.terminals));
    result.tag_terminal_ids = malloc(
        sizeof(*result.tag_terminal_ids) * tag_len);
    if (!result.entries || !result.terminals ||
        !result.tag_terminal_ids) {
        goto done;
    }
    result.entry_len = tag_len;
    for (index = 0u; index < tag_len; index++) {
        int32_t state_id;
        char closure_error[512] = {0};
        if (!tags[index]) {
            pplex_v1_set_error(error_buf, error_buf_size,
                               "lexical projection tag is absent");
            goto done;
        }
        state_id = pplex_v1_tag_state_find(pack, tags[index]);
        if (state_id < 0 || !pack->states[state_id].defined ||
            !ppabi_v1_pack_start_is_closed(
                pack, pack->states[state_id].identity,
                closure_error, sizeof(closure_error))) {
            pplex_v1_set_error(
                error_buf, error_buf_size,
                "lexical projection tag has no closed definition state%s%s",
                closure_error[0] ? ": " : "", closure_error);
            goto done;
        }
        result.entries[index].tag_index = index;
        result.entries[index].state_id = (uint32_t)state_id;
        result.entries[index].tag = atom_deep_copy(
            &result.arena, tags[index]);
        result.entries[index].state_canonical = pplex_v1_text_dup(
            pack->states[state_id].canonical);
        if (!result.entries[index].tag ||
            !result.entries[index].state_canonical) {
            goto done;
        }
    }
    if (tag_len > 1u) {
        qsort(result.entries, tag_len, sizeof(*result.entries),
              pplex_v1_entry_compare);
    }
    for (index = 0u; index < tag_len; index++) {
        PPLexV1Entry *entry = &result.entries[index];
        uint32_t terminal_id = pack->terminal_len + index;
        if (index > 0u &&
            strcmp(result.entries[index - 1u].state_canonical,
                   entry->state_canonical) == 0) {
            pplex_v1_set_error(error_buf, error_buf_size,
                               "lexical projection tag is duplicated");
            goto done;
        }
        entry->terminal_index = index;
        result.terminals[index].terminal_id = terminal_id;
        result.terminals[index].identity = pplex_v1_unary(
            &result.arena, "pp-span-terminal",
            atom_deep_copy(&result.arena, entry->tag));
        result.tag_terminal_ids[entry->tag_index] = terminal_id;
        if (!result.terminals[index].identity)
            goto done;
    }
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.regular_compiler_digest,
           regular_compiler_digest, 65u);
    memcpy(result.nfa_answer_digest, nfa_answer_digest, 65u);
    pplex_v1_plan_digest(&result, result.plan_digest);
    pplex_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    pplex_v1_plan_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "failed to build lexical projection");
    }
    return ok;
}

static int32_t pplex_v1_entry_find_state(const PPLexV1Plan *plan,
                                         uint32_t state_id) {
    uint32_t index;
    for (index = 0u; index < plan->entry_len; index++) {
        if (plan->entries[index].state_id == state_id)
            return (int32_t)index;
    }
    return -1;
}

static bool pplex_v1_plan_matches(const PPABIV1Pack *pack,
                                  const PPLexV1Plan *plan) {
    char digest[65];
    uint32_t index;

    if (!pack || !plan || plan->entry_len == 0u ||
        !plan->entries || !plan->terminals ||
        !plan->tag_terminal_ids ||
        !pplex_v1_digest_valid(plan->base_pack_digest) ||
        !pplex_v1_digest_valid(plan->regular_compiler_digest) ||
        !pplex_v1_digest_valid(plan->nfa_answer_digest) ||
        !pplex_v1_digest_valid(plan->plan_digest) ||
        strcmp(pack->pack_digest, plan->base_pack_digest) != 0 ||
        pack->terminal_len == UINT32_MAX ||
        plan->entry_len > UINT32_MAX - pack->terminal_len) {
        return false;
    }
    for (index = 0u; index < plan->entry_len; index++) {
        const PPLexV1Entry *entry = &plan->entries[index];
        const PPNativeV1TerminalExtension *terminal =
            &plan->terminals[index];
        uint32_t other;
        if (entry->terminal_index != index ||
            entry->tag_index >= plan->entry_len ||
            entry->state_id >= pack->state_len || !entry->tag ||
            !entry->state_canonical ||
            !pack->states[entry->state_id].defined ||
            !ppabi_v1_pack_start_is_closed(
                pack, pack->states[entry->state_id].identity, NULL, 0u) ||
            !pplex_v1_expr_head(
                pack->states[entry->state_id].identity, "pp-def", 1u) ||
            !atom_eq(pack->states[entry->state_id].identity->expr.elems[1],
                     entry->tag) ||
            strcmp(entry->state_canonical,
                   pack->states[entry->state_id].canonical) != 0 ||
            terminal->terminal_id != pack->terminal_len + index ||
            !pplex_v1_expr_head(
                terminal->identity, "pp-span-terminal", 1u) ||
            !atom_eq(terminal->identity->expr.elems[1], entry->tag) ||
            plan->tag_terminal_ids[entry->tag_index] !=
                terminal->terminal_id ||
            (index > 0u &&
             strcmp(plan->entries[index - 1u].state_canonical,
                    entry->state_canonical) >= 0)) {
            return false;
        }
        for (other = 0u; other < index; other++) {
            if (plan->entries[other].tag_index == entry->tag_index ||
                plan->entries[other].state_id == entry->state_id) {
                return false;
            }
        }
    }
    pplex_v1_plan_digest(plan, digest);
    return strcmp(digest, plan->plan_digest) == 0;
}

bool pplex_v1_grammar_build(const PPABIV1Pack *pack,
                            const PPLexV1Plan *plan,
                            CettaLpNativeGrammar *grammar,
                            char *error_buf,
                            size_t error_buf_size) {
    uint32_t production_index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !pplex_v1_plan_matches(pack, plan)) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "bad lexical projection grammar inputs");
        return false;
    }
    if (!ppnative_v1_grammar_build(pack, grammar)) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "failed to copy ParserPack grammar");
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
            int32_t entry_index;
            if (item->kind != CETTA_LP_NATIVE_SYMBOL_HL)
                continue;
            entry_index = pplex_v1_entry_find_state(plan, item->name);
            if (entry_index < 0)
                continue;
            item->kind = CETTA_LP_NATIVE_SYMBOL_TM;
            item->name = plan->terminals[
                plan->entries[entry_index].terminal_index].terminal_id;
            item->scope = 0u;
        }
    }
    return true;
}

void pplex_v1_witness_table_init(PPLexV1WitnessTable *table) {
    if (!table)
        return;
    memset(table, 0, sizeof(*table));
    arena_init(&table->arena);
    table->outcome = PPLEX_V1_WITNESS_COMPLETED;
}

void pplex_v1_witness_table_free(PPLexV1WitnessTable *table) {
    if (!table)
        return;
    free(table->values);
    arena_free(&table->arena);
    memset(table, 0, sizeof(*table));
}

bool pplex_v1_witness_family_prepare(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    PPNativeV1PreparedStartFamily *family,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGrammar grammar;
    uint32_t *start_state_ids = NULL;
    uint32_t index;
    bool ok = false;

    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!family || !pplex_v1_plan_matches(pack, plan) ||
        (size_t)plan->entry_len > SIZE_MAX / sizeof(*start_state_ids)) {
        pplex_v1_set_error(
            error_buf, error_buf_size,
            "bad lexical witness family inputs");
        goto done;
    }
    start_state_ids = malloc(
        sizeof(*start_state_ids) * (size_t)plan->entry_len);
    if (!start_state_ids || !ppnative_v1_grammar_build(pack, &grammar))
        goto done;
    for (index = 0u; index < plan->entry_len; index++)
        start_state_ids[index] = plan->entries[index].state_id;
    if (!ppnative_v1_prepared_start_family_prepare(
            family, &grammar, plan->plan_digest,
            start_state_ids, plan->entry_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    ok = true;

done:
    free(start_state_ids);
    cetta_lp_native_grammar_free(&grammar);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        pplex_v1_set_error(
            error_buf, error_buf_size,
            "failed to prepare lexical witness family");
    }
    return ok;
}

static int32_t pplex_v1_entry_find_tag(const PPLexV1Plan *plan,
                                       uint32_t tag_index) {
    uint32_t index;
    for (index = 0u; index < plan->entry_len; index++) {
        if (plan->entries[index].tag_index == tag_index)
            return (int32_t)index;
    }
    return -1;
}

static bool pplex_v1_source_receipt_valid(
    uint32_t source_pass_count,
    uint32_t decoded_byte_len,
    uint32_t input_byte_len) {
    return (source_pass_count == 0u && decoded_byte_len == 0u) ||
        (source_pass_count == 1u && decoded_byte_len == input_byte_len);
}

static int32_t pplex_v1_forest_root_find(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t state_id,
    uint32_t left,
    uint32_t right) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < forest->root_len; index++) {
        uint32_t node_index = forest->roots[index];
        const CettaLpNativeUtf8ForestNode *node;
        if (node_index >= forest->node_len)
            return -1;
        node = &forest->nodes[node_index];
        if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL ||
            node->symbol_id != state_id || node->scalar_left != left ||
            node->scalar_right != right) {
            continue;
        }
        if (found >= 0)
            return -1;
        found = (int32_t)node_index;
    }
    return found;
}

static bool pplex_v1_replay_root_value(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t root_node,
    uint32_t replay_depth,
    uint32_t result_limit,
    Arena *out_arena,
    Atom **out_value,
    uint32_t *out_value_len,
    PPNativeV1Outcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result replay;
    uint32_t selected_root = root_node;
    bool ok = false;

    *out_value = NULL;
    *out_value_len = 0u;
    *outcome = PPNATIVE_V1_COMPLETED;
    ppnative_v1_result_init(&replay);
    replay.forest = *forest;
    replay.forest.roots = &selected_root;
    replay.forest.root_len = 1u;
    if (!ppnative_v1_finish(
            &replay, pack, start_state, replay_depth, result_limit,
            error_buf, error_buf_size)) {
        goto done;
    }
    *outcome = replay.outcome;
    *out_value_len = replay.semantic_result_len;
    if (replay.outcome != PPNATIVE_V1_COMPLETED ||
        replay.semantic_result_len != 1u) {
        ok = true;
        goto done;
    }
    if (!pplex_v1_expr_head(replay.semantic_results[0], "result", 2u)) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "lexical root replay produced a malformed result");
        goto done;
    }
    *out_value = atom_deep_copy(
        out_arena, replay.semantic_results[0]->expr.elems[1]);
    if (!*out_value)
        goto done;
    ok = true;

done:
    memset(&replay.forest, 0, sizeof(replay.forest));
    ppnative_v1_result_free(&replay);
    return ok;
}

static bool pplex_v1_witness_table_build(
    PPLexV1Backend backend,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    const PPNativeV1PreparedStartFamily *prepared_family,
    const PPNativeV1TerminalPlan *prepared_terminals,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size) {
    PPLexV1WitnessTable result;
    PPNativeV1TerminalPlan local_terminals = {0};
    const PPNativeV1TerminalPlan *terminals = prepared_terminals;
    CettaLpNativeGrammar grammar;
    RSDFAV1ParserLattice scalar_lattice;
    uint32_t *excluded_tags = NULL;
    uint32_t token_index;
    bool ok = false;

    pplex_v1_witness_table_init(&result);
    cetta_lp_native_grammar_init(&grammar);
    rsdfa_v1_parser_lattice_init(&scalar_lattice);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !scan || !out || !pplex_v1_plan_matches(pack, plan) ||
        scan->outcome != RSDFA_V1_SCAN_COMPLETED ||
        !pplex_v1_source_receipt_valid(
            scan->source_pass_count, scan->decoded_byte_len,
            scan->input_byte_len) ||
        scan->tag_len != plan->entry_len ||
        ((prepared_family == NULL) != (prepared_terminals == NULL)) ||
        work_limit == 0u || replay_depth == 0u || result_limit == 0u) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "bad lexical witness realization inputs");
        goto done;
    }
    result.values = calloc(
        scan->token_len ? scan->token_len : 1u, sizeof(*result.values));
    excluded_tags = malloc(
        sizeof(*excluded_tags) * (scan->tag_len ? scan->tag_len : 1u));
    if (!result.values || !excluded_tags)
        goto done;
    result.len = scan->token_len;
    result.source_pass_count = scan->source_pass_count;
    memcpy(result.plan_digest, plan->plan_digest, 65u);
    for (token_index = 0u; token_index < scan->tag_len; token_index++)
        excluded_tags[token_index] = UINT32_MAX;
    if ((!terminals && !ppnative_v1_terminal_plan_build(
            pack, &local_terminals, error_buf, error_buf_size)) ||
        (!prepared_family && !ppnative_v1_grammar_build(pack, &grammar))) {
        goto done;
    }
    if (!terminals)
        terminals = &local_terminals;
    if (!rsdfa_v1_composite_parser_lattice_build(
            scan, terminals->terminals, terminals->len,
            excluded_tags, scan->tag_len, &scalar_lattice,
            error_buf, error_buf_size) ||
        !cetta_lp_native_utf8_lattice_validate(
            &scalar_lattice.lattice, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            pplex_v1_set_error(error_buf, error_buf_size,
                               "failed to build lexical witness carrier");
        }
        goto done;
    }
    for (token_index = 0u; token_index < scan->token_len; token_index++) {
        const RSDFAV1Token *token = &scan->tokens[token_index];
        CettaLpNativeUtf8Forest forest;
        RSDFAV1ParserLattice token_lattice;
        uint32_t token_scalar_len;
        int32_t entry_index;
        int32_t root_node;
        Atom *value = NULL;
        uint32_t value_len = 0u;
        PPNativeV1Outcome replay_outcome = PPNATIVE_V1_COMPLETED;
        bool parsed;

        cetta_lp_native_utf8_forest_init(&forest);
        rsdfa_v1_parser_lattice_init(&token_lattice);
        entry_index = pplex_v1_entry_find_tag(plan, token->tag);
        if (entry_index < 0) {
            cetta_lp_native_utf8_forest_free(&forest);
            pplex_v1_set_error(error_buf, error_buf_size,
                               "DFA token has no lexical projection entry");
            goto done;
        }
        token_scalar_len = token->end_scalar - token->start_scalar;
        if (!rsdfa_v1_parser_lattice_slice_prevalidated(
                &scalar_lattice.lattice,
                token->start_scalar, token->end_scalar, &token_lattice,
                error_buf, error_buf_size)) {
            cetta_lp_native_utf8_forest_free(&forest);
            rsdfa_v1_parser_lattice_free(&token_lattice);
            goto done;
        }
        if (backend == PPLEX_V1_BACKEND_GLL) {
            if (prepared_family) {
                const CettaLpNativeGllPrepared *gll = NULL;
                const CettaLpNativeGlrPrepared *glr = NULL;
                parsed =
                    ppnative_v1_prepared_start_family_lookup(
                        prepared_family, plan->plan_digest,
                        plan->entries[entry_index].state_id,
                        &gll, &glr, error_buf, error_buf_size) &&
                    cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
                        gll, &token_lattice.lattice, 0u, work_limit,
                        &forest, error_buf, error_buf_size);
            } else {
                parsed =
                    cetta_lp_native_gll_parse_utf8_lattice_forest_from_complete(
                        &grammar, plan->entries[entry_index].state_id,
                        &token_lattice.lattice, 0u, work_limit,
                        &forest, error_buf, error_buf_size);
            }
        } else {
            if (prepared_family) {
                const CettaLpNativeGllPrepared *gll = NULL;
                const CettaLpNativeGlrPrepared *glr = NULL;
                parsed =
                    ppnative_v1_prepared_start_family_lookup(
                        prepared_family, plan->plan_digest,
                        plan->entries[entry_index].state_id,
                        &gll, &glr, error_buf, error_buf_size) &&
                    cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
                        glr, &token_lattice.lattice, 0u, work_limit,
                        &forest, error_buf, error_buf_size);
            } else {
                parsed = cetta_lp_native_glr_parse_utf8_lattice_forest_from(
                    &grammar, plan->entries[entry_index].state_id,
                    &token_lattice.lattice, 0u, work_limit,
                    &forest, error_buf, error_buf_size);
            }
        }
        rsdfa_v1_parser_lattice_free(&token_lattice);
        if (!parsed) {
            cetta_lp_native_utf8_forest_free(&forest);
            goto done;
        }
        result.parser_runs++;
        if (UINT64_MAX - result.work_item_len < forest.work_item_len) {
            cetta_lp_native_utf8_forest_free(&forest);
            pplex_v1_set_error(error_buf, error_buf_size,
                               "lexical witness work count overflow");
            goto done;
        }
        result.work_item_len += forest.work_item_len;
        if (forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
            result.outcome = PPLEX_V1_WITNESS_RECOGNIZER_LIMIT;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "%s lexical witness work limit %u at token %u",
                backend == PPLEX_V1_BACKEND_GLL ? "GLL" : "GLR",
                work_limit, token_index);
            cetta_lp_native_utf8_forest_free(&forest);
            goto finish;
        }
        root_node = pplex_v1_forest_root_find(
            &forest, plan->entries[entry_index].state_id,
            0u, token_scalar_len);
        if (root_node < 0) {
            result.outcome = PPLEX_V1_WITNESS_RECOGNITION_MISMATCH;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "DFA token %u has no exact ParserPack root", token_index);
            cetta_lp_native_utf8_forest_free(&forest);
            goto finish;
        }
        if (!pplex_v1_replay_root_value(
                pack, pack->states[plan->entries[entry_index].state_id].identity,
                &forest, (uint32_t)root_node, replay_depth, result_limit,
                &result.arena, &value, &value_len, &replay_outcome,
                error_buf, error_buf_size)) {
            cetta_lp_native_utf8_forest_free(&forest);
            goto done;
        }
        cetta_lp_native_utf8_forest_free(&forest);
        if (replay_outcome == PPNATIVE_V1_REPLAY_DEPTH) {
            result.outcome = PPLEX_V1_WITNESS_REPLAY_DEPTH;
            (void)snprintf(result.detail, sizeof(result.detail),
                           "lexical witness replay depth %u at token %u",
                           replay_depth, token_index);
            goto finish;
        }
        if (replay_outcome == PPNATIVE_V1_RESULT_LIMIT) {
            result.outcome = PPLEX_V1_WITNESS_RESULT_LIMIT;
            (void)snprintf(result.detail, sizeof(result.detail),
                           "lexical witness result limit %u at token %u",
                           result_limit, token_index);
            goto finish;
        }
        if (replay_outcome != PPNATIVE_V1_COMPLETED || value_len != 1u ||
            !value) {
            result.outcome = PPLEX_V1_WITNESS_NONFUNCTIONAL;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "lexical token %u has %u semantic values",
                token_index, value_len);
            goto finish;
        }
        result.values[token_index] = value;
    }

finish:
    pplex_v1_witness_table_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(excluded_tags);
    rsdfa_v1_parser_lattice_free(&scalar_lattice);
    cetta_lp_native_grammar_free(&grammar);
    ppnative_v1_terminal_plan_free(&local_terminals);
    pplex_v1_witness_table_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "failed to realize lexical witnesses");
    }
    return ok;
}

bool pplex_v1_witness_table_build_gll(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size) {
    return pplex_v1_witness_table_build(
        PPLEX_V1_BACKEND_GLL, pack, plan, scan, NULL, NULL,
        descriptor_limit,
        replay_depth, result_limit, out, error_buf, error_buf_size);
}

bool pplex_v1_witness_table_build_glr(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size) {
    return pplex_v1_witness_table_build(
        PPLEX_V1_BACKEND_GLR, pack, plan, scan, NULL, NULL, work_limit,
        replay_depth, result_limit, out, error_buf, error_buf_size);
}

bool pplex_v1_witness_table_build_gll_prepared(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    const PPNativeV1PreparedStartFamily *family,
    const PPNativeV1TerminalPlan *terminals,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size) {
    return pplex_v1_witness_table_build(
        PPLEX_V1_BACKEND_GLL, pack, plan, scan, family, terminals,
        descriptor_limit, replay_depth, result_limit,
        out, error_buf, error_buf_size);
}

bool pplex_v1_witness_table_build_glr_prepared(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    const PPNativeV1PreparedStartFamily *family,
    const PPNativeV1TerminalPlan *terminals,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size) {
    return pplex_v1_witness_table_build(
        PPLEX_V1_BACKEND_GLR, pack, plan, scan, family, terminals,
        work_limit, replay_depth, result_limit,
        out, error_buf, error_buf_size);
}

static bool pplex_v1_parse(
    PPLexV1Backend backend,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result result;
    PPNativeV1ForestExtension extension;
    CettaLpNativeGrammar grammar;
    int32_t start_id;
    char closure_error[512] = {0};
    uint32_t root_index;
    bool ok = false;

    ppnative_v1_result_init(&result);
    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state || !lattice || !out ||
        !pplex_v1_plan_matches(pack, plan) ||
        !pplex_v1_source_receipt_valid(
            lattice->source_pass_count, lattice->decoded_byte_len,
            lattice->input_byte_len) ||
        work_limit == 0u ||
        replay_depth == 0u || result_limit == 0u ||
        (witness_len > 0u && !witness_values)) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "bad projected ParserPack parse arguments");
        goto done;
    }
    start_id = ppnative_v1_state_find(pack, start_state);
    if (start_id < 0 || !ppabi_v1_pack_start_is_closed(
                            pack, start_state, closure_error,
                            sizeof(closure_error))) {
        result.outcome = PPNATIVE_V1_UNSUPPORTED_OPEN_PACK;
        (void)snprintf(result.detail, sizeof(result.detail), "%s",
                       closure_error[0] ? closure_error
                                        : "start state is absent");
        goto finish;
    }
    if (pplex_v1_entry_find_state(plan, (uint32_t)start_id) >= 0) {
        pplex_v1_set_error(error_buf, error_buf_size,
                           "lexical projection cannot replace its start state");
        goto done;
    }
    if (!pplex_v1_grammar_build(
            pack, plan, &grammar, error_buf, error_buf_size)) {
        goto done;
    }
    if (backend == PPLEX_V1_BACKEND_GLL) {
        if (!cetta_lp_native_gll_parse_utf8_lattice_forest_complete(
                &grammar, (uint32_t)start_id, lattice, work_limit,
                &result.forest, error_buf, error_buf_size)) {
            goto done;
        }
    } else {
        if (!cetta_lp_native_glr_parse_utf8_lattice_forest(
                &grammar, (uint32_t)start_id, lattice, work_limit,
                &result.forest, error_buf, error_buf_size)) {
            goto done;
        }
    }
    if (result.forest.outcome ==
        CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        result.outcome = PPNATIVE_V1_RECOGNIZER_LIMIT;
        (void)snprintf(
            result.detail, sizeof(result.detail), "%s work limit %u",
            backend == PPLEX_V1_BACKEND_GLL ? "GLL" : "GLR",
            work_limit);
        goto finish;
    }
    for (root_index = 0u; root_index < result.forest.root_len;
         root_index++) {
        uint32_t node_index = result.forest.roots[root_index];
        if (node_index < result.forest.node_len &&
            result.forest.nodes[node_index].scalar_left == 0u &&
            result.forest.nodes[node_index].scalar_right ==
                result.forest.scalar_len) {
            result.accepted = true;
            break;
        }
    }
    extension = (PPNativeV1ForestExtension){
        .terminals = plan->terminals,
        .terminal_len = plan->entry_len,
        .witness_values = witness_values,
        .witness_len = witness_len,
    };
    if (!ppnative_v1_finish_extended(
            &result, pack, start_state, &extension,
            replay_depth, result_limit, error_buf, error_buf_size)) {
        goto done;
    }

finish:
    ppnative_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_grammar_free(&grammar);
    ppnative_v1_result_free(&result);
    return ok;
}

bool pplex_v1_gll_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return pplex_v1_parse(
        PPLEX_V1_BACKEND_GLL, pack, start_state, plan, lattice,
        witness_values, witness_len, descriptor_limit,
        replay_depth, result_limit, out, error_buf, error_buf_size);
}

bool pplex_v1_glr_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return pplex_v1_parse(
        PPLEX_V1_BACKEND_GLR, pack, start_state, plan, lattice,
        witness_values, witness_len, work_limit,
        replay_depth, result_limit, out, error_buf, error_buf_size);
}
