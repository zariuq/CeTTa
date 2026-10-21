#include "equation_presentation.h"

#include "match.h"
#include "parser.h"
#include "symbol.h"

Atom *cetta_presentation_parse_one(Arena *owner, const char *source) {
    size_t position = 0u;
    Atom *term = owner && source
        ? parse_sexpr(owner, source, &position)
        : NULL;
    return term && parser_rest_is_delimiters(source, &position)
        ? term
        : NULL;
}

bool cetta_patterns_overlap(Atom *left, Atom *right) {
    if (!left || !right) return false;
    /* Only the verdict is kept; the unifier and its syntax are dropped. */
    Arena scratch;
    arena_init(&scratch);
    Bindings overlap;
    bindings_init(&overlap);
    bool result = match_atoms(left, right, &overlap, &scratch);
    bindings_free(&overlap);
    arena_free(&scratch);
    return result;
}

bool cetta_equation_presentation_exact(
    Arena *scratch, Space *space, const char *head_name,
    const char *const *expected_sources, size_t expected_count) {
    if (!scratch || !space || !head_name || !expected_sources ||
        expected_count == 0u || !g_symbols) {
        return false;
    }
    SymbolId head = symbol_intern_cstr(g_symbols, head_name);
    SpaceEquationCursor cursor;
    if (head == SYMBOL_ID_NONE ||
        !space_equation_cursor_init(space, head, &cursor)) {
        return false;
    }
    Atom **expected = arena_alloc(
        scratch, expected_count * sizeof(*expected));
    Atom **expected_lhs = arena_alloc(
        scratch, expected_count * sizeof(*expected_lhs));
    if (!expected || !expected_lhs) return false;
    for (size_t index = 0u; index < expected_count; index++) {
        expected[index] = cetta_presentation_parse_one(
            scratch, expected_sources[index]);
        expected_lhs[index] = expected[index] &&
                expected[index]->kind == ATOM_EXPR &&
                expected[index]->expr.len == 3u &&
                atom_is_symbol(expected[index]->expr.elems[0], "=")
            ? expected[index]->expr.elems[1]
            : NULL;
        if (!expected_lhs[index]) return false;
    }

    size_t matched = 0u;
    for (;;) {
        SpaceEquationOccurrenceId occurrence_id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &occurrence_id);
        if (step == SPACE_EQUATION_CURSOR_END) break;
        if (step != SPACE_EQUATION_CURSOR_ITEM) return false;
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(
                occurrence_id, &occurrence)) {
            return false;
        }
        if (matched < expected_count &&
            atom_alpha_eq(occurrence.equation, expected[matched])) {
            matched++;
            continue;
        }

        /* The Space cursor intentionally includes wildcard-head equations:
         * they might match this operation.  An equation whose structured
         * head is provably disjoint is merely a consumer of the operation,
         * not another definition of it.  Any overlapping wildcard still
         * invalidates the exact admitted presentation. */
        for (size_t index = 0u; index < expected_count; index++) {
            if (cetta_patterns_overlap(
                    occurrence.lhs, expected_lhs[index])) {
                return false;
            }
        }
    }
    return matched == expected_count;
}
