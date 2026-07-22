#include "parser_atom_projection_v1.h"
#include "parser_atom_projection_events_v1.h"
#include "parser_atom_projection_domain_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message)                                         \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL: %s\n", (message));                    \
            return false;                                                 \
        }                                                                 \
    } while (0)

static const char PLAN[] =
    "(sap-plan-v1 node pair cons nil cp document expression "
    "(sap-cons (sap-text-rule word symbol raw) "
    "(sap-cons (sap-text-rule string symbol decoded-quoted) "
    "(sap-cons (sap-variable-rule variable raw no-anonymous) sap-nil))) "
    "(sap-cons (sap-map-wrapper simple-escape reject "
    "(sap-cons (sap-codepoint-map 110 10) "
    "(sap-cons (sap-codepoint-map 114 13) "
    "(sap-cons (sap-codepoint-map 116 9) "
    "(sap-cons (sap-codepoint-map 34 34) "
    "(sap-cons (sap-codepoint-map 39 39) "
    "(sap-cons (sap-codepoint-map 92 92) sap-nil))))))) "
    "(sap-cons (sap-hex-wrapper byte-escape 2 2 127) "
    "(sap-cons (sap-hex-wrapper unicode-escape 1 6 1114111) sap-nil))))";

static const char BASIC_DOCUMENT[] =
    "(result (node document "
    "(cons (node word (cons (cp 955) nil)) "
    "(cons (node expression "
    "(cons (node word (cons (cp 102) (cons (cp 111) "
    "(cons (cp 111) nil)))) "
    "(cons (node variable (cons (cp 120) nil)) "
    "(cons (node variable (cons (cp 120) nil)) "
    "(cons (node string "
    "(cons (cp 97) "
    "(cons (node simple-escape (cp 110)) "
    "(cons (node byte-escape (cons (cp 52) (cons (cp 49) nil))) "
    "(cons (node unicode-escape "
    "(cons (cp 48) (cons (cp 51) (cons (cp 98) (cons (cp 98) nil))))) "
    "nil))))) nil))))) "
    "(cons (node variable (cons (cp 120) nil)) nil)))) nil)";

static Atom *parse(Arena *arena, const char *text) {
    Atom *term = NULL;
    char error[512] = {0};

    if (!fh_ground_term_v1_parse(
            arena, (const uint8_t *)text, strlen(text),
            &term, error, sizeof(error))) {
        fprintf(stderr, "FAIL: ground-term parse: %s\n", error);
        return NULL;
    }
    return term;
}

static Atom *parse_stream(Arena *arena, FILE *input) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    Atom *term = NULL;
    char error[512] = {0};

    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                fprintf(stderr, "FAIL: compiled plan is too large\n");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                fprintf(stderr, "FAIL: cannot allocate compiled plan\n");
                goto done;
            }
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(input)) {
            fprintf(stderr, "FAIL: cannot read compiled plan\n");
            goto done;
        }
        break;
    }
    while (len > 0u &&
           (bytes[len - 1u] == (uint8_t)'\n' ||
            bytes[len - 1u] == (uint8_t)'\r')) {
        len--;
    }
    if (!fh_ground_term_v1_parse(
            arena, bytes, len, &term, error, sizeof(error))) {
        fprintf(stderr, "FAIL: compiled-plan parse: %s\n", error);
        term = NULL;
    }

done:
    free(bytes);
    return term;
}

static Atom *wrap_plan(Arena *arena, Atom *plan) {
    static const char ZERO_DIGEST[] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    char digest[65];
    Atom *arguments[6];
    Atom **elements;
    Atom *artifact;
    uint32_t index;

    if (!plan || !fh_ground_term_v1_render(
            plan, &canonical, &canonical_len, NULL, 0u)) {
        free(canonical);
        return NULL;
    }
    cetta_native_sha256_hex(canonical, canonical_len, digest);
    free(canonical);
    arguments[0] = atom_symbol(arena, "he-v1");
    arguments[1] = atom_symbol(arena, ZERO_DIGEST);
    arguments[2] = atom_symbol(arena, ZERO_DIGEST);
    arguments[3] = atom_symbol(arena, ZERO_DIGEST);
    arguments[4] = atom_symbol(arena, digest);
    arguments[5] = plan;
    elements = malloc(sizeof(*elements) * 7u);
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, "sap-artifact-v1");
    for (index = 0u; index < 6u; index++)
        elements[index + 1u] = arguments[index];
    artifact = atom_expr(arena, elements, 7u);
    free(elements);
    return artifact;
}

static bool project(PPAtomProjectionV1Plan *plan,
                    Arena *arena,
                    const char *semantic,
                    uint32_t depth_limit,
                    Atom ***out_atoms,
                    uint32_t *out_len,
                    char *error,
                    size_t error_cap) {
    Atom *term = parse(arena, semantic);

    return term && ppatom_projection_v1_project_document(
        plan, term, arena, depth_limit, out_atoms, out_len,
        error, error_cap);
}

static bool basic_document_gate(PPAtomProjectionV1Plan *plan,
                                Arena *arena,
                                size_t *passed) {
    Atom **atoms = NULL;
    uint32_t len = 0u;
    Atom *expression;
    Atom *first_variable;
    Atom *second_variable;
    char error[512] = {0};

    CHECK(project(plan, arena, BASIC_DOCUMENT, 256u,
                  &atoms, &len, error, sizeof(error)), error);
    CHECK(len == 3u, "document did not produce three top-level Atoms");
    CHECK(atoms && atom_is_symbol(atoms[0], "\xce\xbb"),
          "Unicode word did not project to its host symbol");
    expression = atoms[1];
    CHECK(expression && expression->kind == ATOM_EXPR &&
          expression->expr.len == 4u,
          "headless expression projection changed arity");
    CHECK(atom_is_symbol(expression->expr.elems[0], "foo"),
          "expression head changed");
    first_variable = expression->expr.elems[1];
    second_variable = expression->expr.elems[2];
    CHECK(first_variable && first_variable->kind == ATOM_VAR &&
          first_variable == second_variable,
          "repeated variable spelling did not share identity within a form");
    CHECK(atom_is_symbol(expression->expr.elems[3],
                         "\"a\nA\xce\xbb\""),
          "decoded string spelling changed");
    CHECK(atoms[2] && atoms[2]->kind == ATOM_VAR &&
          atoms[2]->var_id != first_variable->var_id,
          "variable identity leaked across top-level reader boundaries");
    (*passed) += 7u;
    return true;
}

static bool empty_shapes_gate(PPAtomProjectionV1Plan *plan,
                              Arena *arena,
                              size_t *passed) {
    Atom **atoms = (Atom **)plan;
    uint32_t len = UINT32_MAX;
    char error[512] = {0};

    CHECK(project(plan, arena,
                  "(result (node document nil) nil)", 64u,
                  &atoms, &len, error, sizeof(error)), error);
    CHECK(atoms == NULL && len == 0u,
          "empty document did not produce an empty Atom stream");
    error[0] = '\0';
    CHECK(project(plan, arena,
                  "(result (node document "
                  "(cons (node expression nil) nil)) nil)", 64u,
                  &atoms, &len, error, sizeof(error)), error);
    CHECK(atoms && len == 1u && atoms[0]->kind == ATOM_EXPR &&
          atoms[0]->expr.len == 0u,
          "empty expression did not project to the unit expression");
    error[0] = '\0';
    CHECK(project(plan, arena,
                  "(result (node document "
                  "(cons (node variable nil) nil)) nil)", 64u,
                  &atoms, &len, error, sizeof(error)), error);
    CHECK(atoms && len == 1u && atoms[0]->kind == ATOM_VAR &&
          strcmp(atom_name_cstr(atoms[0]), "") == 0,
          "empty variable spelling changed");
    (*passed) += 6u;
    return true;
}

static bool semantic_rejection_gate(PPAtomProjectionV1Plan *plan,
                                    Arena *arena,
                                    size_t *passed) {
    static const char *const rejected[] = {
        "(result (node document nil) trailing)",
        "(result (node document (cons (node mystery nil) nil)) nil)",
        "(result (node document (cons (node word nil) nil)) nil)",
        "(result (node document (cons (node word "
        "(node simple-escape (cp 110))) nil)) nil)",
        "(result (node document (cons (node string "
        "(node simple-escape (cp 120))) nil)) nil)",
        "(result (node document (cons (node string "
        "(node byte-escape (cons (cp 56) (cons (cp 48) nil)))) nil)) nil)",
        "(result (node document (cons (node string "
        "(node byte-escape (cons (cp 52) nil))) nil)) nil)",
        "(result (node document (cons (node string "
        "(node unicode-escape (cons (cp 100) (cons (cp 56) "
        "(cons (cp 48) (cons (cp 48) nil)))))) nil)) nil)",
        "(result (node document (node word (cp 97))) nil)",
    };
    size_t index;

    for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]);
         index++) {
        Atom **atoms = (Atom **)plan;
        Atom **event_atoms = (Atom **)plan;
        Atom *semantic;
        uint32_t len = UINT32_MAX;
        uint32_t event_len = UINT32_MAX;
        char error[512] = {0};

        CHECK(!project(plan, arena, rejected[index], 128u,
                       &atoms, &len, error, sizeof(error)),
              "malformed semantic result was accepted");
        CHECK(atoms == NULL && len == 0u && error[0] != '\0',
              "semantic rejection was not fail-closed");
        semantic = parse(arena, rejected[index]);
        error[0] = '\0';
        CHECK(semantic &&
              !ppatom_projection_events_v1_project_document(
                  plan, semantic, arena, 128u,
                  &event_atoms, &event_len, error, sizeof(error)) &&
              event_atoms == NULL && event_len == 0u && error[0] != '\0',
              "semantic-event bridge accepted a malformed result");
        (*passed) += 2u;
    }
    {
        Atom **atoms = (Atom **)plan;
        uint32_t len = UINT32_MAX;
        char error[512] = {0};

        CHECK(!project(
                  plan, arena,
                  "(result (node document (cons (node expression "
                  "(cons (node expression nil) nil)) nil)) nil)",
                  2u, &atoms, &len, error, sizeof(error)),
              "depth-limited expression projection was accepted");
        CHECK(atoms == NULL && len == 0u && error[0] != '\0',
              "depth rejection was not fail-closed");
        (*passed)++;
    }
    return true;
}

static bool embedded_nul_gate(PPAtomProjectionV1Plan *plan,
                              Arena *arena,
                              size_t *passed) {
    Atom **atoms = NULL;
    uint32_t len = 0u;
    const char *spelling;
    char error[512] = {0};

    CHECK(project(
              plan, arena,
              "(result (node document (cons (node word "
              "(cons (cp 97) (cons (cp 0) (cons (cp 98) nil)))) nil)) nil)",
              64u, &atoms, &len, error, sizeof(error)),
          error);
    CHECK(atoms && len == 1u && atoms[0]->kind == ATOM_SYMBOL,
          "embedded-NUL word did not project to one symbol");
    spelling = symbol_bytes(g_symbols, atoms[0]->sym_id);
    CHECK(symbol_len(g_symbols, atoms[0]->sym_id) == 3u &&
          memcmp(spelling, "a\0b", 3u) == 0,
          "embedded-NUL symbol spelling changed");
    (*passed) += 2u;
    return true;
}

static bool invalid_plan(PPAtomProjectionV1Plan *plan,
                         Arena *arena,
                         const char *text) {
    Atom *term = wrap_plan(arena, parse(arena, text));
    char error[512] = {0};

    return term &&
        !ppatom_projection_v1_plan_load(
            plan, term, error, sizeof(error)) &&
        error[0] != '\0';
}

static bool plan_rejection_and_atomicity_gate(
    PPAtomProjectionV1Plan *plan,
    Arena *arena,
    size_t *passed) {
    static const char *const rejected[] = {
        "(sap-plan-v1 node node cons nil cp document expression "
        "(sap-cons (sap-text-rule word symbol raw) sap-nil) sap-nil)",
        "(sap-plan-v1 node pair cons nil cp document expression "
        "(sap-cons (sap-text-rule word symbol raw) "
        "(sap-cons (sap-variable-rule word raw no-anonymous) sap-nil)) "
        "sap-nil)",
        "(sap-plan-v1 node pair cons nil cp document expression "
        "(sap-cons (sap-text-rule word symbol raw) sap-nil) "
        "(sap-cons (sap-map-wrapper word identity "
        "(sap-cons (sap-codepoint-map 97 97) sap-nil)) sap-nil))",
        "(sap-plan-v1 node pair cons nil cp document expression "
        "(sap-cons (sap-text-rule word symbol raw) sap-nil) "
        "(sap-cons (sap-map-wrapper escape identity "
        "(sap-cons (sap-codepoint-map 97 97) "
        "(sap-cons (sap-codepoint-map 97 98) sap-nil))) sap-nil))",
        "(sap-plan-v1 node pair cons nil cp document expression "
        "(sap-cons (sap-text-rule word symbol raw) sap-nil) "
        "(sap-cons (sap-hex-wrapper escape 3 2 255) sap-nil))",
    };
    size_t index;
    Atom **atoms = NULL;
    uint32_t len = 0u;
    char error[512] = {0};

    for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]);
         index++) {
        CHECK(invalid_plan(plan, arena, rejected[index]),
              "invalid projection plan was accepted");
        (*passed)++;
    }
    {
        Atom *bad_digest = wrap_plan(arena, parse(arena, PLAN));
        CHECK(bad_digest && bad_digest->kind == ATOM_EXPR &&
              bad_digest->expr.len == 7u,
              "could not construct the digest mutation");
        bad_digest->expr.elems[5] = atom_symbol(
            arena,
            "0000000000000000000000000000000000000000000000000000000000000000");
        error[0] = '\0';
        CHECK(!ppatom_projection_v1_plan_load(
                  plan, bad_digest, error, sizeof(error)) &&
              error[0] != '\0',
              "projection digest mutation was accepted");
        (*passed)++;
    }
    CHECK(project(plan, arena, BASIC_DOCUMENT, 256u,
                  &atoms, &len, error, sizeof(error)),
          "failed plan replacement destroyed the valid plan");
    CHECK(len == 3u && atom_is_symbol(atoms[0], "\xce\xbb"),
          "valid plan changed after failed atomic replacements");
    (*passed) += 2u;
    return true;
}

static bool long_text_gate(PPAtomProjectionV1Plan *plan,
                           Arena *arena,
                           size_t *passed) {
    static const uint32_t scalar_count = 70000u;
    Atom *nil = atom_symbol(arena, "nil");
    Atom *cons = atom_symbol(arena, "cons");
    Atom *codepoint = atom_symbol(arena, "cp");
    Atom *node = atom_symbol(arena, "node");
    Atom *text = nil;
    Atom *word;
    Atom *document;
    Atom *semantic;
    Atom **atoms = NULL;
    uint32_t len = 0u;
    uint32_t index;
    char error[512] = {0};
    const char *spelling;

    for (index = 0u; index < scalar_count; index++) {
        Atom *scalar = atom_expr2(arena, codepoint, atom_int(arena, 97));
        text = atom_expr3(arena, cons, scalar, text);
    }
    word = atom_expr3(arena, node, atom_symbol(arena, "word"), text);
    document = atom_expr3(
        arena, node, atom_symbol(arena, "document"),
        atom_expr3(arena, cons, word, nil));
    semantic = atom_expr3(
        arena, atom_symbol(arena, "result"), document, nil);
    CHECK(ppatom_projection_v1_project_document(
              plan, semantic, arena, 8u, &atoms, &len,
              error, sizeof(error)),
          error);
    CHECK(len == 1u && atoms && atoms[0]->kind == ATOM_SYMBOL,
          "long text did not project to one symbol");
    spelling = atom_name_cstr(atoms[0]);
    CHECK(strlen(spelling) == scalar_count && spelling[0] == 'a' &&
          spelling[scalar_count - 1u] == 'a',
          "long text projection changed content or length");
    (*passed) += 2u;
    return true;
}

static int domain_ascii_membership(const RSDFAV1Program *program,
                                   const char *text,
                                   char *error,
                                   size_t error_cap) {
    uint32_t codepoints[16];
    uint32_t byte_offsets[17];
    CettaLpNativeUtf8ScalarView view;
    RSDFAV1Token tokens[1];
    RSDFAV1CursorScanResult result;
    size_t len = strlen(text);
    size_t index;

    if (len > sizeof(codepoints) / sizeof(codepoints[0]))
        return -1;
    for (index = 0u; index < len; index++) {
        if ((unsigned char)text[index] > 0x7fu)
            return -1;
        codepoints[index] = (unsigned char)text[index];
        byte_offsets[index] = (uint32_t)index;
    }
    byte_offsets[len] = (uint32_t)len;
    view = (CettaLpNativeUtf8ScalarView){
        .codepoints = codepoints,
        .byte_offsets = byte_offsets,
        .scalar_len = (uint32_t)len,
        .input_byte_len = (uint32_t)len,
        .decoded_byte_len = 0u,
        .source_pass_count = 0u,
    };
    if (!rsdfa_v1_program_scan_cursor_longest(
            program, &view, 0u, 1024u, tokens, 1u,
            &result, error, error_cap) ||
        result.outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED) {
        return -1;
    }
    return result.accept_len == 1u && tokens[0].tag == 0u &&
        tokens[0].end_scalar == len;
}

static bool projection_domain_gate(PPAtomProjectionV1Plan *plan,
                                   size_t *passed) {
    static const uint32_t newline_escape[] = {(uint32_t)'n'};
    static const uint32_t byte_escape[] = {(uint32_t)'7', (uint32_t)'f'};
    static const uint32_t unicode_escape[] = {
        (uint32_t)'0', (uint32_t)'3', (uint32_t)'b', (uint32_t)'b'
    };
    static const uint32_t unknown_escape[] = {(uint32_t)'q'};
    static const uint32_t overlarge_byte[] = {(uint32_t)'8', (uint32_t)'0'};
    static const uint32_t surrogate[] = {
        (uint32_t)'d', (uint32_t)'8', (uint32_t)'0', (uint32_t)'0'
    };
    RSDFAV1Program programs[3];
    PPAtomProjectionV1WrapperView wrappers[3];
    RSDFAV1InclusionResult inclusion;
    RSDFAV1BuildOutcome outcome;
    uint32_t wrapper_count = 0u;
    uint32_t index;
    uint32_t decoded = 0u;
    uint32_t saved_state_len;
    char error[512] = {0};
    bool ok = false;

    for (index = 0u; index < 3u; index++)
        rsdfa_v1_program_init(&programs[index]);
    rsdfa_v1_inclusion_result_init(&inclusion);
    CHECK(ppatom_projection_v1_plan_wrapper_count(
              plan, &wrapper_count, error, sizeof(error)) &&
          wrapper_count == 3u,
          "projection wrapper inventory changed");
    for (index = 0u; index < 3u; index++) {
        CHECK(ppatom_projection_v1_plan_wrapper(
                  plan, index, &wrappers[index],
                  error, sizeof(error)),
              error);
        CHECK(ppatom_projection_domain_v1_program(
                  plan, index, 4096u, 16384u,
                  &programs[index], &outcome,
                  error, sizeof(error)) &&
              outcome == RSDFA_V1_BUILD_COMPLETED &&
              rsdfa_v1_program_validate(
                  &programs[index], error, sizeof(error)),
              error);
    }
    CHECK(strcmp(wrappers[0].label, "simple-escape") == 0 &&
          wrappers[0].kind == PPATOM_PROJECTION_V1_WRAPPER_MAP &&
          wrappers[0].map_default == PPATOM_PROJECTION_V1_MAP_REJECT &&
          wrappers[0].mapping_len == 6u &&
          domain_ascii_membership(
              &programs[0], "n", error, sizeof(error)) == 1 &&
          domain_ascii_membership(
              &programs[0], "x", error, sizeof(error)) == 0,
          "mapped-wrapper domain changed");
    CHECK(strcmp(wrappers[1].label, "byte-escape") == 0 &&
          wrappers[1].kind == PPATOM_PROJECTION_V1_WRAPPER_HEX &&
          wrappers[1].min_digits == 2u &&
          wrappers[1].max_digits == 2u &&
          wrappers[1].max_value == 127u &&
          domain_ascii_membership(
              &programs[1], "41", error, sizeof(error)) == 1 &&
          domain_ascii_membership(
              &programs[1], "7f", error, sizeof(error)) == 1 &&
          domain_ascii_membership(
              &programs[1], "80", error, sizeof(error)) == 0 &&
          domain_ascii_membership(
              &programs[1], "4", error, sizeof(error)) == 0,
          "byte-hex projection domain changed");
    CHECK(strcmp(wrappers[2].label, "unicode-escape") == 0 &&
          wrappers[2].kind == PPATOM_PROJECTION_V1_WRAPPER_HEX &&
          wrappers[2].min_digits == 1u &&
          wrappers[2].max_digits == 6u &&
          wrappers[2].max_value == 1114111u &&
          domain_ascii_membership(
              &programs[2], "0", error, sizeof(error)) == 1 &&
          domain_ascii_membership(
              &programs[2], "03bb", error, sizeof(error)) == 1 &&
          domain_ascii_membership(
              &programs[2], "10FFFF", error, sizeof(error)) == 1 &&
          domain_ascii_membership(
              &programs[2], "", error, sizeof(error)) == 0 &&
          domain_ascii_membership(
              &programs[2], "d800", error, sizeof(error)) == 0 &&
          domain_ascii_membership(
              &programs[2], "110000", error, sizeof(error)) == 0 &&
          domain_ascii_membership(
              &programs[2], "00003bb", error, sizeof(error)) == 0,
          "Unicode-hex projection domain changed");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &programs[1], 0u, &programs[2], 0u, 4096u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_COMPLETED &&
          inclusion.included,
          "byte-hex domain was not included in Unicode-hex domain");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &programs[2], 0u, &programs[1], 0u, 4096u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_COMPLETED &&
          !inclusion.included && inclusion.counterexample_len == 1u &&
          inclusion.counterexample[0] == (uint32_t)'0',
          "Unicode/byte domain non-inclusion witness changed");
    CHECK(ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 0u, newline_escape, 1u, &decoded,
              error, sizeof(error)) && decoded == 10u,
          "mapped-wrapper direct execution changed");
    CHECK(ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 1u, byte_escape, 2u, &decoded,
              error, sizeof(error)) && decoded == 127u,
          "byte-wrapper direct execution changed");
    CHECK(ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 2u, unicode_escape, 4u, &decoded,
              error, sizeof(error)) && decoded == 955u,
          "Unicode-wrapper direct execution changed");
    decoded = 0xfeedu;
    CHECK(!ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 0u, unknown_escape, 1u, &decoded,
              error, sizeof(error)) && decoded == 0xfeedu,
          "unmapped direct wrapper execution did not fail atomically");
    CHECK(!ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 1u, overlarge_byte, 2u, &decoded,
              error, sizeof(error)) && decoded == 0xfeedu,
          "overlarge direct byte wrapper did not fail atomically");
    CHECK(!ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 2u, surrogate, 4u, &decoded,
              error, sizeof(error)) && decoded == 0xfeedu,
          "surrogate direct Unicode wrapper did not fail atomically");
    CHECK(!ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
              plan, 3u, newline_escape, 1u, &decoded,
              error, sizeof(error)) && decoded == 0xfeedu,
          "invalid direct wrapper index did not fail atomically");
    saved_state_len = programs[0].state_len;
    CHECK(ppatom_projection_domain_v1_program(
              plan, 0u, 1u, 16u, &programs[0], &outcome,
              error, sizeof(error)) &&
          outcome == RSDFA_V1_BUILD_STATE_LIMIT &&
          programs[0].state_len == saved_state_len,
          "resource-limited domain compilation was not atomic");
    CHECK(!ppatom_projection_v1_plan_wrapper(
              plan, 3u, &wrappers[0], error, sizeof(error)) &&
          !ppatom_projection_domain_v1_program(
              plan, 3u, 32u, 32u, &programs[0], &outcome,
              error, sizeof(error)) &&
          programs[0].state_len == saved_state_len,
          "invalid wrapper lookup did not fail atomically");
    *passed += 19u;
    ok = true;

    rsdfa_v1_inclusion_result_free(&inclusion);
    for (index = 0u; index < 3u; index++)
        rsdfa_v1_program_free(&programs[index]);
    return ok;
}

static bool projection_plan_view_gate(PPAtomProjectionV1Plan *plan,
                                      size_t *passed) {
    static const char *const sources[] = {"word", "string", "variable"};
    PPAtomProjectionV1CarrierView carriers;
    PPAtomProjectionV1RuleView rule;
    uint32_t rule_count = 0u;
    uint32_t index;
    char error[512] = {0};

    CHECK(ppatom_projection_v1_plan_carriers(
              plan, &carriers, error, sizeof(error)), error);
    CHECK(strcmp(carriers.node_label, "node") == 0 &&
          strcmp(carriers.pair_label, "pair") == 0 &&
          strcmp(carriers.cons_label, "cons") == 0 &&
          strcmp(carriers.nil_label, "nil") == 0 &&
          strcmp(carriers.codepoint_label, "cp") == 0 &&
          strcmp(carriers.document_label, "document") == 0 &&
          strcmp(carriers.expression_label, "expression") == 0,
          "projection carrier view changed");
    CHECK(ppatom_projection_v1_plan_rule_count(
              plan, &rule_count, error, sizeof(error)) &&
          rule_count == 3u,
          "projection rule inventory changed");
    for (index = 0u; index < rule_count; index++) {
        CHECK(ppatom_projection_v1_plan_rule(
                  plan, index, &rule, error, sizeof(error)), error);
        CHECK(strcmp(rule.source_label, sources[index]) == 0,
              "projection rule ordering changed");
    }
    CHECK(ppatom_projection_v1_plan_rule(
              plan, 0u, &rule, error, sizeof(error)) &&
          rule.kind == PPATOM_PROJECTION_V1_RULE_TEXT &&
          rule.output_kind == PPATOM_PROJECTION_V1_OUTPUT_SYMBOL &&
          rule.text_mode == PPATOM_PROJECTION_V1_TEXT_RAW,
          "word projection rule view changed");
    CHECK(ppatom_projection_v1_plan_rule(
              plan, 1u, &rule, error, sizeof(error)) &&
          rule.kind == PPATOM_PROJECTION_V1_RULE_TEXT &&
          rule.output_kind == PPATOM_PROJECTION_V1_OUTPUT_SYMBOL &&
          rule.text_mode == PPATOM_PROJECTION_V1_TEXT_DECODED_QUOTED,
          "string projection rule view changed");
    CHECK(ppatom_projection_v1_plan_rule(
              plan, 2u, &rule, error, sizeof(error)) &&
          rule.kind == PPATOM_PROJECTION_V1_RULE_VARIABLE &&
          rule.text_mode == PPATOM_PROJECTION_V1_TEXT_RAW,
          "variable projection rule view changed");
    error[0] = '\0';
    CHECK(!ppatom_projection_v1_plan_rule(
              plan, rule_count, &rule, error, sizeof(error)) &&
          error[0] != '\0',
          "invalid projection rule view did not fail closed");
    *passed += 10u;
    return true;
}

typedef struct {
    VarId left;
    VarId right;
} AlphaVarPair;

typedef struct {
    AlphaVarPair pairs[64];
    uint32_t len;
} AlphaVarMap;

static bool alpha_atom_equal(Atom *left, Atom *right, AlphaVarMap *variables) {
    uint32_t index;

    if (!left || !right || left->kind != right->kind)
        return left == right;
    if (left->kind == ATOM_VAR) {
        if (left->sym_id != right->sym_id)
            return false;
        for (index = 0u; index < variables->len; index++) {
            if (variables->pairs[index].left == left->var_id ||
                variables->pairs[index].right == right->var_id) {
                return variables->pairs[index].left == left->var_id &&
                    variables->pairs[index].right == right->var_id;
            }
        }
        if (variables->len ==
            sizeof(variables->pairs) / sizeof(variables->pairs[0])) {
            return false;
        }
        variables->pairs[variables->len++] =
            (AlphaVarPair){left->var_id, right->var_id};
        return true;
    }
    if (left->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len)
            return false;
        for (index = 0u; index < left->expr.len; index++) {
            if (!alpha_atom_equal(
                    left->expr.elems[index], right->expr.elems[index],
                    variables)) {
                return false;
            }
        }
        return true;
    }
    return atom_eq(left, right);
}

static bool alpha_document_equal(Atom **left,
                                 uint32_t left_len,
                                 Atom **right,
                                 uint32_t right_len) {
    uint32_t index;

    if (left_len != right_len)
        return false;
    for (index = 0u; index < left_len; index++) {
        AlphaVarMap variables = {{{0u, 0u}}, 0u};
        if (!alpha_atom_equal(left[index], right[index], &variables))
            return false;
    }
    return true;
}

static bool event_emit(PPAtomProjectionEventsV1Builder *builder,
                       PPAtomProjectionEventsV1Kind kind,
                       uint32_t operand,
                       char *error,
                       size_t error_cap) {
    return ppatom_projection_events_v1_builder_emit(
        builder, (PPAtomProjectionEventsV1Event){kind, operand},
        error, error_cap);
}

static bool event_basic_document(PPAtomProjectionEventsV1Builder *builder,
                                 char *error,
                                 size_t error_cap) {
#define EVENT(kind, operand)                                               \
    do {                                                                   \
        if (!event_emit(builder, (kind), (operand), error, error_cap))     \
            return false;                                                  \
    } while (0)
    EVENT(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, 0x03bbu);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'f');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'o');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'o');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'x');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'x');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'a');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'n');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 1u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'4');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'1');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 1u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'0');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'3');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'b');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'b');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 1u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_END, 0u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'x');
    EVENT(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 2u);
    EVENT(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END, 0u);
#undef EVENT
    return true;
}

static bool projection_event_equivalence_gate(
    PPAtomProjectionV1Plan *plan,
    PPAtomProjectionEventsV1Builder *builder,
    Arena *arena,
    size_t *passed) {
    Atom **tree_atoms = NULL;
    Atom **event_atoms = NULL;
    Atom **bridged_atoms = NULL;
    Atom *semantic;
    uint32_t tree_len = 0u;
    uint32_t event_len = 0u;
    uint32_t bridged_len = 0u;
    char error[512] = {0};

    CHECK(project(plan, arena, BASIC_DOCUMENT, 256u,
                  &tree_atoms, &tree_len, error, sizeof(error)), error);
    semantic = parse(arena, BASIC_DOCUMENT);
    CHECK(semantic && ppatom_projection_events_v1_project_document(
              plan, semantic, arena, 256u,
              &bridged_atoms, &bridged_len, error, sizeof(error)),
          error);
    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 256u, error, sizeof(error)), error);
    CHECK(event_basic_document(builder, error, sizeof(error)), error);
    CHECK(ppatom_projection_events_v1_builder_finish(
              builder, &event_atoms, &event_len, error, sizeof(error)),
          error);
    CHECK(alpha_document_equal(
              tree_atoms, tree_len, event_atoms, event_len),
          "event projection differs from neutral-tree projection");
    CHECK(alpha_document_equal(
              tree_atoms, tree_len, bridged_atoms, bridged_len) &&
          alpha_document_equal(
              event_atoms, event_len, bridged_atoms, bridged_len),
          "semantic-event bridge differs from either projection path");
    CHECK(event_len == 3u && atom_is_symbol(event_atoms[0], "\xce\xbb") &&
          event_atoms[1]->kind == ATOM_EXPR &&
          event_atoms[1]->expr.len == 4u &&
          event_atoms[1]->expr.elems[1] ==
              event_atoms[1]->expr.elems[2] &&
          atom_is_symbol(event_atoms[1]->expr.elems[3],
                         "\"a\nA\xce\xbb\"") &&
          event_atoms[2]->kind == ATOM_VAR &&
          event_atoms[2]->var_id !=
              event_atoms[1]->expr.elems[1]->var_id,
          "event projection changed the basic document semantics");
    *passed += 8u;
    return true;
}

static bool projection_event_empty_gate(
    PPAtomProjectionV1Plan *plan,
    PPAtomProjectionEventsV1Builder *builder,
    Arena *arena,
    size_t *passed) {
    Atom **atoms = (Atom **)plan;
    uint32_t len = UINT32_MAX;
    char error[512] = {0};

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &atoms, &len, error, sizeof(error)),
          error);
    CHECK(atoms == NULL && len == 0u,
          "empty event document did not produce an empty Atom stream");

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &atoms, &len, error, sizeof(error)),
          error);
    CHECK(atoms && len == 1u && atoms[0]->kind == ATOM_EXPR &&
          atoms[0]->expr.len == 0u,
          "empty event expression changed shape");

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     2u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     2u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &atoms, &len, error, sizeof(error)),
          error);
    CHECK(atoms && len == 1u && atoms[0]->kind == ATOM_VAR &&
          atoms[0]->sym_id == SYMBOL_ID_NONE,
          "empty event variable changed spelling");
    *passed += 6u;
    return true;
}

static bool projection_event_leaf_kind_gate(
    PPAtomProjectionV1Plan *plan,
    PPAtomProjectionEventsV1Builder *builder,
    Arena *arena,
    size_t *passed) {
    static const char STRING_PLAN[] =
        "(sap-plan-v1 node pair cons nil cp document expression "
        "(sap-cons (sap-text-rule text string decoded) sap-nil) "
        "(sap-cons (sap-map-wrapper escape identity "
        "(sap-cons (sap-codepoint-map 97 98) sap-nil)) sap-nil))";
    PPAtomProjectionV1Plan string_plan;
    Atom **tree_atoms = NULL;
    Atom **event_atoms = NULL;
    uint32_t tree_len = 0u;
    uint32_t event_len = 0u;
    char error[512] = {0};

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'a', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'b', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &event_atoms, &event_len, error, sizeof(error)),
          error);
    CHECK(event_atoms && event_len == 1u &&
          event_atoms[0]->kind == ATOM_SYMBOL &&
          symbol_len(g_symbols, event_atoms[0]->sym_id) == 3u &&
          memcmp(symbol_bytes(g_symbols, event_atoms[0]->sym_id),
                 "a\0b", 3u) == 0,
          "event projector changed an embedded-NUL symbol");

    ppatom_projection_v1_plan_init(&string_plan);
    CHECK(ppatom_projection_v1_plan_load(
              &string_plan,
              wrap_plan(arena, parse(arena, STRING_PLAN)),
              error, sizeof(error)),
          error);
    CHECK(project(
              &string_plan, arena,
              "(result (node document (cons (node text "
              "(cons (cp 97) (cons (cp 955) nil))) nil)) nil)",
              16u, &tree_atoms, &tree_len, error, sizeof(error)),
          error);
    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, &string_plan, arena, 16u,
              error, sizeof(error)),
          error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'a', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     0x03bbu, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &event_atoms, &event_len, error, sizeof(error)),
          error);
    CHECK(tree_len == 1u && event_len == 1u &&
          event_atoms[0]->kind == ATOM_GROUNDED &&
          event_atoms[0]->ground.gkind == GV_STRING &&
          strcmp(event_atoms[0]->ground.sval, "a\xce\xbb") == 0 &&
          atom_eq(tree_atoms[0], event_atoms[0]),
          "event projector changed grounded-string output");

    CHECK(project(
              &string_plan, arena,
              "(result (node document "
              "(cons (node text (node escape (cp 120))) "
              "(cons (node text (node escape (cp 97))) nil))) nil)",
              16u, &tree_atoms, &tree_len, error, sizeof(error)),
          error);
    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, &string_plan, arena, 16u,
              error, sizeof(error)),
          error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'x', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'a', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &event_atoms, &event_len, error, sizeof(error)),
          error);
    CHECK(tree_len == 2u && event_len == 2u &&
          atom_eq(tree_atoms[0], event_atoms[0]) &&
          atom_eq(tree_atoms[1], event_atoms[1]) &&
          strcmp(event_atoms[0]->ground.sval, "x") == 0 &&
          strcmp(event_atoms[1]->ground.sval, "b") == 0,
          "event projector changed identity/default map behavior");

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, &string_plan, arena, 16u,
              error, sizeof(error)),
          error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     0u, error, sizeof(error)) &&
          !event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                      0u, error, sizeof(error)) &&
          error[0] != '\0',
          "event projector accepted embedded NUL in a grounded string");
    ppatom_projection_v1_plan_free(&string_plan);
    *passed += 11u;
    return true;
}

static bool event_expect_rejection(
    PPAtomProjectionV1Plan *plan,
    PPAtomProjectionEventsV1Builder *builder,
    Arena *arena,
    const PPAtomProjectionEventsV1Event *events,
    size_t event_len,
    uint32_t depth_limit) {
    char error[512] = {0};
    size_t index;

    if (!ppatom_projection_events_v1_builder_bind(
            builder, plan, arena, depth_limit, error, sizeof(error))) {
        return false;
    }
    for (index = 0u; index < event_len; index++) {
        if (!ppatom_projection_events_v1_builder_emit(
                builder, events[index], error, sizeof(error))) {
            return error[0] != '\0';
        }
    }
    {
        Atom **atoms = (Atom **)plan;
        uint32_t len = UINT32_MAX;
        return !ppatom_projection_events_v1_builder_finish(
                   builder, &atoms, &len, error, sizeof(error)) &&
            atoms == NULL && len == 0u && error[0] != '\0';
    }
}

static bool projection_event_rejection_gate(
    PPAtomProjectionV1Plan *plan,
    PPAtomProjectionEventsV1Builder *builder,
    Arena *arena,
    size_t *passed) {
#define E(kind, operand) {(kind), (operand)}
    static const PPAtomProjectionEventsV1Event scalar_outside[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'a')};
    static const PPAtomProjectionEventsV1Event wrapper_in_raw[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 0u)};
    static const PPAtomProjectionEventsV1Event empty_map[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 0u)};
    static const PPAtomProjectionEventsV1Event short_hex[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'4'),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 1u)};
    static const PPAtomProjectionEventsV1Event non_hex[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'g'),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'0'),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 1u)};
    static const PPAtomProjectionEventsV1Event byte_overbound[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'8'),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'0'),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 1u)};
    static const PPAtomProjectionEventsV1Event hex_surrogate[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 2u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'d'),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'8'),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'0'),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'0'),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 2u)};
    static const PPAtomProjectionEventsV1Event unmapped[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, (uint32_t)'x'),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END, 0u)};
    static const PPAtomProjectionEventsV1Event mismatched_leaf[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_END, 1u)};
    static const PPAtomProjectionEventsV1Event invalid_rule[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 99u)};
    static const PPAtomProjectionEventsV1Event invalid_wrapper[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 99u)};
    static const PPAtomProjectionEventsV1Event surrogate[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_SCALAR, 0xd800u)};
    static const PPAtomProjectionEventsV1Event nested_wrapper[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 1u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN, 1u)};
    static const PPAtomProjectionEventsV1Event premature_document_end[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END, 0u)};
    static const PPAtomProjectionEventsV1Event bad_structural_operand[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 1u)};
    static const PPAtomProjectionEventsV1Event invalid_kind[] = {
        E((PPAtomProjectionEventsV1Kind)99, 0u)};
    static const PPAtomProjectionEventsV1Event incomplete_document[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u)};
    static const PPAtomProjectionEventsV1Event depth_exceeded[] = {
        E(PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN, 0u),
        E(PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN, 0u)};
    struct {
        const PPAtomProjectionEventsV1Event *events;
        size_t len;
        uint32_t depth;
    } cases[] = {
        {scalar_outside, sizeof(scalar_outside) / sizeof(*scalar_outside), 8u},
        {wrapper_in_raw, sizeof(wrapper_in_raw) / sizeof(*wrapper_in_raw), 8u},
        {empty_map, sizeof(empty_map) / sizeof(*empty_map), 8u},
        {short_hex, sizeof(short_hex) / sizeof(*short_hex), 8u},
        {non_hex, sizeof(non_hex) / sizeof(*non_hex), 8u},
        {byte_overbound,
         sizeof(byte_overbound) / sizeof(*byte_overbound), 8u},
        {hex_surrogate,
         sizeof(hex_surrogate) / sizeof(*hex_surrogate), 8u},
        {unmapped, sizeof(unmapped) / sizeof(*unmapped), 8u},
        {mismatched_leaf, sizeof(mismatched_leaf) / sizeof(*mismatched_leaf), 8u},
        {invalid_rule, sizeof(invalid_rule) / sizeof(*invalid_rule), 8u},
        {invalid_wrapper, sizeof(invalid_wrapper) / sizeof(*invalid_wrapper), 8u},
        {surrogate, sizeof(surrogate) / sizeof(*surrogate), 8u},
        {nested_wrapper, sizeof(nested_wrapper) / sizeof(*nested_wrapper), 8u},
        {premature_document_end,
         sizeof(premature_document_end) / sizeof(*premature_document_end), 8u},
        {bad_structural_operand,
         sizeof(bad_structural_operand) / sizeof(*bad_structural_operand), 8u},
        {invalid_kind, sizeof(invalid_kind) / sizeof(*invalid_kind), 8u},
        {incomplete_document,
         sizeof(incomplete_document) / sizeof(*incomplete_document), 8u},
        {depth_exceeded,
         sizeof(depth_exceeded) / sizeof(*depth_exceeded), 2u},
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CHECK(event_expect_rejection(
                  plan, builder, arena, cases[index].events,
                  cases[index].len, cases[index].depth),
              "malformed projection event stream was accepted");
        (*passed)++;
    }
#undef E
    return true;
}

static bool projection_event_atomicity_gate(
    PPAtomProjectionV1Plan *plan,
    PPAtomProjectionEventsV1Builder *builder,
    Arena *arena,
    size_t *passed) {
    size_t before = arena->live_bytes;
    Atom **atoms = (Atom **)plan;
    uint32_t len = UINT32_MAX;
    char error[512] = {0};

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'a', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     0u, error, sizeof(error)),
          error);
    CHECK(arena->live_bytes > before,
          "atomicity canary did not allocate a projected Atom");
    CHECK(!event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                      (uint32_t)'b', error, sizeof(error)) &&
          error[0] != '\0' && arena->live_bytes == before,
          "invalid event did not roll the arena back to the bind mark");

    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END,
                     0u, error, sizeof(error)) &&
          ppatom_projection_events_v1_builder_finish(
              builder, &atoms, &len, error, sizeof(error)),
          "event builder could not recover after an atomic rejection");
    CHECK(atoms == NULL && len == 0u,
          "rebound event builder changed the empty document");

    before = arena->live_bytes;
    CHECK(ppatom_projection_events_v1_builder_bind(
              builder, plan, arena, 16u, error, sizeof(error)), error);
    CHECK(event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                     0u, error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                     (uint32_t)'z', error, sizeof(error)) &&
          event_emit(builder, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                     0u, error, sizeof(error)) &&
          arena->live_bytes > before,
          "unfinished-builder rollback canary did not allocate an Atom");
    ppatom_projection_events_v1_builder_free(builder);
    CHECK(arena->live_bytes == before,
          "freeing an unfinished event builder did not roll back its arena");
    ppatom_projection_events_v1_builder_init(builder);
    *passed += 8u;
    return true;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    PPAtomProjectionV1Plan plan;
    PPAtomProjectionEventsV1Builder event_builder;
    PPAtomProjectionV1ProvenanceView provenance;
    Atom *compiled_plan;
    char error[512] = {0};
    size_t passed = 0u;
    bool ok;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    ppatom_projection_v1_plan_init(&plan);
    ppatom_projection_events_v1_builder_init(&event_builder);

    if (argc == 1) {
        compiled_plan = wrap_plan(&arena, parse(&arena, PLAN));
    } else if (argc == 2 && strcmp(argv[1], "--plan-stdin") == 0) {
        compiled_plan = parse_stream(&arena, stdin);
    } else {
        fprintf(stderr, "usage: %s [--plan-stdin]\n", argv[0]);
        compiled_plan = NULL;
    }
    ok = compiled_plan && ppatom_projection_v1_plan_load(
        &plan, compiled_plan, error, sizeof(error));
    if (!ok)
        fprintf(stderr, "FAIL: %s\n", error);
    else {
        passed++;
        ok = ppatom_projection_v1_plan_provenance(
            &plan, &provenance, error, sizeof(error));
        if (!ok) {
            fprintf(stderr, "FAIL: %s\n", error);
        } else if (strcmp(provenance.profile, "he-v1") != 0 ||
                   strlen(provenance.source_digest) != 64u ||
                   strlen(provenance.reader_digest) != 64u ||
                   strlen(provenance.compiler_digest) != 64u ||
                   strlen(provenance.plan_digest) != 64u) {
            fprintf(stderr, "FAIL: projection provenance changed shape\n");
            ok = false;
        } else {
            passed += 2u;
        }
    }
    ok = ok &&
        basic_document_gate(&plan, &arena, &passed) &&
        empty_shapes_gate(&plan, &arena, &passed) &&
        semantic_rejection_gate(&plan, &arena, &passed) &&
        embedded_nul_gate(&plan, &arena, &passed) &&
        plan_rejection_and_atomicity_gate(&plan, &arena, &passed) &&
        long_text_gate(&plan, &arena, &passed) &&
        projection_domain_gate(&plan, &passed) &&
        projection_plan_view_gate(&plan, &passed) &&
        projection_event_equivalence_gate(
            &plan, &event_builder, &arena, &passed) &&
        projection_event_empty_gate(
            &plan, &event_builder, &arena, &passed) &&
        projection_event_leaf_kind_gate(
            &plan, &event_builder, &arena, &passed) &&
        projection_event_rejection_gate(
            &plan, &event_builder, &arena, &passed) &&
        projection_event_atomicity_gate(
            &plan, &event_builder, &arena, &passed);

    ppatom_projection_events_v1_builder_free(&event_builder);
    ppatom_projection_v1_plan_free(&plan);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    if (!ok)
        return 1;
    printf("(ParserAtomProjectionV1NativeSummary %zu 0)\n", passed);
    return 0;
}
