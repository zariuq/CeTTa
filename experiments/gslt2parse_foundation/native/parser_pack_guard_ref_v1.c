#include "parser_pack_guard_ref_v1.h"

#include "finite_horn_ground_term_v1.h"

#include "symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PPGUARD_REF_V1_GLL = 0,
    PPGUARD_REF_V1_GLR = 1
} PPGuardRefV1Backend;

typedef struct {
    PPGuardRelationV1Match *data;
    uint32_t len;
    uint32_t cap;
} PPGuardRefV1MatchVec;

static void ppguard_ref_v1_set_error(char *buf,
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

static bool ppguard_ref_v1_expr_head(const Atom *atom,
                                     const char *head,
                                     CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static int32_t ppguard_ref_v1_entry_find_tag(
    const PPGuardPlanV1 *plan,
    const Atom *tag) {
    uint32_t index;

    for (index = 0u; index < plan->entry_len; index++) {
        if (atom_eq(plan->entries[index].tag, (Atom *)tag))
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppguard_ref_v1_body_state(
    const PPABIV1Pack *pack,
    const Atom *body) {
    const Atom *name;
    uint32_t index;

    if (!ppguard_ref_v1_expr_head(body, "sir-ref", 1u) ||
        body->expr.elems[1]->kind != ATOM_SYMBOL) {
        return -1;
    }
    name = body->expr.elems[1];
    for (index = 0u; index < pack->state_len; index++) {
        const Atom *identity = pack->states[index].identity;
        if (ppguard_ref_v1_expr_head(identity, "pp-def", 1u) &&
            atom_eq(identity->expr.elems[1], (Atom *)name)) {
            return (int32_t)index;
        }
    }
    return -1;
}

bool ppguard_ref_v1_witness_family_prepare(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
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
    if (!pack || !plan || !family || plan->entry_len == 0u ||
        !plan->entries ||
        (size_t)plan->entry_len > SIZE_MAX / sizeof(*start_state_ids)) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "bad positive-guard witness family inputs");
        goto done;
    }
    start_state_ids = malloc(
        sizeof(*start_state_ids) * (size_t)plan->entry_len);
    if (!start_state_ids || !ppnative_v1_grammar_build(pack, &grammar))
        goto done;
    for (index = 0u; index < plan->entry_len; index++) {
        int32_t state_id = ppguard_ref_v1_body_state(
            pack, plan->entries[index].body);
        if (state_id < 0 ||
            !ppabi_v1_pack_start_is_closed(
                pack, pack->states[state_id].identity,
                error_buf, error_buf_size)) {
            if (!error_buf || error_buf[0] == '\0') {
                ppguard_ref_v1_set_error(
                    error_buf, error_buf_size,
                    "positive guard body is outside the closed-reference fragment");
            }
            goto done;
        }
        start_state_ids[index] = (uint32_t)state_id;
    }
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
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "failed to prepare positive-guard witness family");
    }
    return ok;
}

static int32_t ppguard_ref_v1_forest_root_find(
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
            node->symbol_id != state_id ||
            node->scalar_left != left || node->scalar_right != right) {
            continue;
        }
        if (found >= 0)
            return -1;
        found = (int32_t)node_index;
    }
    return found;
}

static bool ppguard_ref_v1_match_push(
    PPGuardRefV1MatchVec *matches,
    const PPGuardRelationV1Match *match) {
    if (matches->len == matches->cap) {
        uint32_t next_cap = matches->cap ? matches->cap * 2u : 16u;
        PPGuardRelationV1Match *next;
        if (next_cap < matches->cap) {
            return false;
        }
        next = realloc(matches->data, sizeof(*next) * (size_t)next_cap);
        if (!next)
            return false;
        matches->data = next;
        matches->cap = next_cap;
    }
    matches->data[matches->len++] = *match;
    return true;
}

static bool ppguard_ref_v1_result_value(
    Atom *result,
    Atom **value) {
    if (!ppguard_ref_v1_expr_head(result, "result", 2u))
        return false;
    *value = result->expr.elems[1];
    return true;
}

static bool ppguard_ref_v1_proof_has_local_extent(
    const Atom *proof,
    uint32_t scalar_len) {
    const Atom *extent;

    if (!ppguard_ref_v1_expr_head(proof, "pf-v1", 5u))
        return false;
    extent = proof->expr.elems[2];
    return extent && extent->kind == ATOM_GROUNDED &&
        extent->ground.gkind == GV_INT &&
        extent->ground.ival >= 0 &&
        (uint64_t)extent->ground.ival == (uint64_t)scalar_len;
}

static bool ppguard_ref_v1_replay_matches(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t root_node,
    uint32_t guard_terminal_id,
    const RSDFAV1Token *token,
    uint32_t replay_depth,
    uint32_t result_limit,
    Arena *arena,
    PPGuardRefV1MatchVec *matches,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result replay;
    uint32_t selected_root = root_node;
    Atom *proof = NULL;
    uint32_t index;
    bool ok = false;

    ppnative_v1_result_init(&replay);
    if (forest->scalar_len != token->end_scalar - token->start_scalar) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "closed-reference guard forest is not token-local");
        goto done;
    }
    replay.forest = *forest;
    replay.forest.roots = &selected_root;
    replay.forest.root_len = 1u;
    if (!ppnative_v1_finish(
            &replay, pack, start_state, replay_depth, result_limit,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (replay.outcome != PPNATIVE_V1_COMPLETED ||
        replay.semantic_result_len == 0u ||
        !replay.canonical_forest_materialized ||
        !replay.canonical_forest) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "closed-reference guard witness replay did not complete");
        goto done;
    }
    proof = atom_deep_copy(arena, replay.canonical_forest);
    if (!proof)
        goto done;
    if (!ppguard_ref_v1_proof_has_local_extent(
            proof, token->end_scalar - token->start_scalar)) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "closed-reference guard proof is not token-local");
        goto done;
    }
    for (index = 0u; index < replay.semantic_result_len; index++) {
        Atom *value = NULL;
        Atom *copy;
        PPGuardRelationV1Match match;

        if (!ppguard_ref_v1_result_value(
                replay.semantic_results[index], &value) ||
            !(copy = atom_deep_copy(arena, value))) {
            ppguard_ref_v1_set_error(
                error_buf, error_buf_size,
                "closed-reference guard witness result is malformed");
            goto done;
        }
        match = (PPGuardRelationV1Match){
            .guard_terminal_id = guard_terminal_id,
            .scalar_left = token->start_scalar,
            .body_scalar_right = token->end_scalar,
            .byte_left = token->start_byte,
            .body_byte_right = token->end_byte,
            .value = copy,
            .proof = proof,
        };
        if (!ppguard_ref_v1_match_push(matches, &match))
            goto done;
    }
    ok = true;

done:
    memset(&replay.forest, 0, sizeof(replay.forest));
    ppnative_v1_result_free(&replay);
    return ok;
}

static bool ppguard_ref_v1_relation_build(
    PPGuardRefV1Backend backend,
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const PPNativeV1PreparedStartFamily *prepared_family,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    Arena arena;
    CettaLpNativeGrammar grammar;
    PPGuardRefV1MatchVec matches = {0};
    bool *seen = NULL;
    uint32_t token_index;
    PPGuardRefV1Receipt result = {0};
    bool ok = false;

    arena_init(&arena);
    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !plan || !scan || !base_lattice ||
        !out || !receipt ||
        scan_tag_len != scan->tag_len || scan_tag_len != plan->entry_len ||
        scan->outcome != RSDFA_V1_SCAN_COMPLETED ||
        scan->source_pass_count != base_lattice->source_pass_count ||
        scan->decoded_byte_len != base_lattice->decoded_byte_len ||
        base_lattice->scalar_len != scan->input_scalar_len ||
        base_lattice->input_byte_len != scan->input_byte_len ||
        work_limit == 0u || replay_depth == 0u || result_limit == 0u ||
        !cetta_lp_native_utf8_lattice_validate(
            base_lattice, error_buf, error_buf_size)) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "bad closed-reference guard realization inputs");
        goto done;
    }
    if (scan_tag_len == 0u) {
        if (scan->token_len != 0u ||
            !ppguard_relation_v1_build(
                base_lattice, base_witness_values, base_witness_len,
                NULL, 0u, NULL, 0u, plan->plan_digest,
                out, error_buf, error_buf_size)) {
            if (error_buf && error_buf_size > 0u &&
                error_buf[0] == '\0') {
                ppguard_ref_v1_set_error(
                    error_buf, error_buf_size,
                    "empty guard family did not preserve its base relation");
            }
            goto done;
        }
        result.source_pass_count = base_lattice->source_pass_count;
        memcpy(result.relation_digest, out->relation_digest, 65u);
        *receipt = result;
        ok = true;
        goto done;
    }
    if (!scan_tags) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "nonempty guard family has no scan tags");
        goto done;
    }
    seen = calloc(plan->entry_len, sizeof(*seen));
    if (!seen ||
        (!prepared_family && !ppnative_v1_grammar_build(pack, &grammar)))
        goto done;
    for (token_index = 0u; token_index < scan_tag_len; token_index++) {
        int32_t entry_index = ppguard_ref_v1_entry_find_tag(
            plan, scan_tags[token_index]);
        int32_t state_id;
        char closure[512] = {0};
        if (entry_index < 0 || seen[entry_index]) {
            ppguard_ref_v1_set_error(
                error_buf, error_buf_size,
                "guard NFA tags are not a bijection with the plan");
            goto done;
        }
        seen[entry_index] = true;
        state_id = ppguard_ref_v1_body_state(
            pack, plan->entries[entry_index].body);
        if (state_id < 0 ||
            !ppabi_v1_pack_start_is_closed(
                pack, pack->states[state_id].identity,
                closure, sizeof(closure))) {
            ppguard_ref_v1_set_error(
                error_buf, error_buf_size,
                "positive guard body is outside the closed-reference fragment");
            goto done;
        }
    }
    for (token_index = 0u; token_index < scan->token_len; token_index++) {
        const RSDFAV1Token *token = &scan->tokens[token_index];
        int32_t entry_index;
        int32_t state_id;
        CettaLpNativeUtf8Forest forest;
        RSDFAV1ParserLattice token_lattice;
        uint32_t token_scalar_len;
        int32_t root_node;
        bool parsed;

        if (token->tag >= scan_tag_len ||
            (entry_index = ppguard_ref_v1_entry_find_tag(
                plan, scan_tags[token->tag])) < 0 ||
            (state_id = ppguard_ref_v1_body_state(
                pack, plan->entries[entry_index].body)) < 0) {
            ppguard_ref_v1_set_error(
                error_buf, error_buf_size,
                "guard scan token has no closed-reference plan entry");
            goto done;
        }
        cetta_lp_native_utf8_forest_init(&forest);
        rsdfa_v1_parser_lattice_init(&token_lattice);
        if (!rsdfa_v1_parser_lattice_slice_prevalidated(
                base_lattice, token->start_scalar, token->end_scalar,
                &token_lattice, error_buf, error_buf_size)) {
            cetta_lp_native_utf8_forest_free(&forest);
            rsdfa_v1_parser_lattice_free(&token_lattice);
            goto done;
        }
        token_scalar_len = token->end_scalar - token->start_scalar;
        if (backend == PPGUARD_REF_V1_GLL) {
            if (prepared_family) {
                const CettaLpNativeGllPrepared *gll = NULL;
                const CettaLpNativeGlrPrepared *glr = NULL;
                parsed =
                    ppnative_v1_prepared_start_family_lookup(
                        prepared_family, plan->plan_digest,
                        (uint32_t)state_id, &gll, &glr,
                        error_buf, error_buf_size) &&
                    cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
                        gll, &token_lattice.lattice, 0u, work_limit,
                        &forest, error_buf, error_buf_size);
            } else {
                parsed =
                    cetta_lp_native_gll_parse_utf8_lattice_forest_from_complete(
                        &grammar, (uint32_t)state_id,
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
                        (uint32_t)state_id, &gll, &glr,
                        error_buf, error_buf_size) &&
                    cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
                        glr, &token_lattice.lattice, 0u, work_limit,
                        &forest, error_buf, error_buf_size);
            } else {
                parsed = cetta_lp_native_glr_parse_utf8_lattice_forest_from(
                    &grammar, (uint32_t)state_id,
                    &token_lattice.lattice, 0u, work_limit,
                    &forest, error_buf, error_buf_size);
            }
        }
        rsdfa_v1_parser_lattice_free(&token_lattice);
        if (!parsed ||
            forest.outcome != CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED) {
            cetta_lp_native_utf8_forest_free(&forest);
            if (parsed) {
                ppguard_ref_v1_set_error(
                    error_buf, error_buf_size,
                    "closed-reference guard witness hit its work limit");
            }
            goto done;
        }
        root_node = ppguard_ref_v1_forest_root_find(
            &forest, (uint32_t)state_id,
            0u, token_scalar_len);
        if (root_node < 0 ||
            !ppguard_ref_v1_replay_matches(
                pack, pack->states[state_id].identity,
                &forest, (uint32_t)root_node,
                plan->entries[entry_index].terminal_id,
                token, replay_depth, result_limit,
                &arena, &matches, error_buf, error_buf_size)) {
            cetta_lp_native_utf8_forest_free(&forest);
            if (root_node < 0) {
                ppguard_ref_v1_set_error(
                    error_buf, error_buf_size,
                    "guard NFA span lacks an exact ParserPack witness");
            }
            goto done;
        }
        if (UINT64_MAX - result.work_item_len < forest.work_item_len) {
            cetta_lp_native_utf8_forest_free(&forest);
            ppguard_ref_v1_set_error(
                error_buf, error_buf_size,
                "closed-reference guard work count overflow");
            goto done;
        }
        result.work_item_len += forest.work_item_len;
        result.parser_run_len++;
        cetta_lp_native_utf8_forest_free(&forest);
    }
    if (!ppguard_relation_v1_build(
            base_lattice, base_witness_values, base_witness_len,
            plan->guard_terminal_ids, plan->entry_len,
            matches.data, matches.len, plan->plan_digest,
            out, error_buf, error_buf_size)) {
        goto done;
    }
    result.match_len = matches.len;
    result.source_pass_count = base_lattice->source_pass_count;
    memcpy(result.relation_digest, out->relation_digest, 65u);
    *receipt = result;
    ok = true;

done:
    free(seen);
    free(matches.data);
    cetta_lp_native_grammar_free(&grammar);
    arena_free(&arena);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "failed to realize closed-reference positive guards");
    }
    return ok;
}

bool ppguard_ref_v1_relation_build_gll(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_ref_v1_relation_build(
        PPGUARD_REF_V1_GLL, pack, plan, scan_tags, scan_tag_len,
        scan, base_lattice, base_witness_values, base_witness_len,
        NULL, work_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}

bool ppguard_ref_v1_relation_build_glr(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_ref_v1_relation_build(
        PPGUARD_REF_V1_GLR, pack, plan, scan_tags, scan_tag_len,
        scan, base_lattice, base_witness_values, base_witness_len,
        NULL, work_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}

bool ppguard_ref_v1_relation_build_gll_prepared(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const PPNativeV1PreparedStartFamily *family,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    if (!family) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "prepared positive-guard witness family is absent");
        return false;
    }
    return ppguard_ref_v1_relation_build(
        PPGUARD_REF_V1_GLL, pack, plan, scan_tags, scan_tag_len,
        scan, base_lattice, base_witness_values, base_witness_len,
        family, work_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}

bool ppguard_ref_v1_relation_build_glr_prepared(
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *plan,
    Atom *const *scan_tags,
    uint32_t scan_tag_len,
    const RSDFAV1ScanResult *scan,
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const PPNativeV1PreparedStartFamily *family,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPGuardRelationV1 *out,
    PPGuardRefV1Receipt *receipt,
    char *error_buf,
    size_t error_buf_size) {
    if (!family) {
        ppguard_ref_v1_set_error(
            error_buf, error_buf_size,
            "prepared positive-guard witness family is absent");
        return false;
    }
    return ppguard_ref_v1_relation_build(
        PPGUARD_REF_V1_GLR, pack, plan, scan_tags, scan_tag_len,
        scan, base_lattice, base_witness_values, base_witness_len,
        family, work_limit, replay_depth, result_limit,
        out, receipt, error_buf, error_buf_size);
}
