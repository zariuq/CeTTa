#define main langdef_tool_main
#include "tools/langdef_compile_v1.c"
#undef main
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static Atom *unary(Arena *arena, const char *head, Atom *arg) {
    Atom *items[] = {atom_symbol(arena, head), arg};
    return atom_expr(arena, items, 2u);
}
static Atom *binary(Arena *arena, const char *head, Atom *left, Atom *right) {
    Atom *items[] = {atom_symbol(arena, head), left, right};
    return atom_expr(arena, items, 3u);
}
static Atom *chain(Arena *arena, Atom *leaf, size_t depth) {
    for (size_t i = 0u; i < depth; ++i) leaf = unary(arena, "Box", leaf);
    return leaf;
}
int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena; arena_init_detached(&arena);
    char error[512] = {0};
    Atom *nil = atom_symbol(&arena, "q-nil");
    Atom *box = unary(&arena, "q-sym", atom_symbol(&arena, "Box"));
    Atom *term = binary(&arena, "q-app",
        unary(&arena, "q-sym", atom_symbol(&arena, "Leaf")), nil);
    for (size_t i = 0u; i < 6000u; ++i)
        term = binary(&arena, "q-app", box, binary(&arena, "q-cons", term, nil));
    LangDefByteBufferV1 output = {0};
    assert(langdef_semantic_render_q_term_v1(&output, term,  error, sizeof error));
    assert(output.len == 6000u * 6u + 6u);
    const uint8_t *at = output.bytes;
    for (size_t i = 0u; i < 6000u; ++i) { assert(!memcmp(at, "(Box ", 5u)); at += 5u; }
    assert(!memcmp(at, "(Leaf)", 6u)); at += 6u;
    for (size_t i = 0u; i < 6000u; ++i) assert(*at++ == ')');
    free(output.bytes);
    output = (LangDefByteBufferV1){0};
    Atom *var = unary(&arena, "q-var", atom_symbol(&arena, "q-zero"));
    assert(langdef_semantic_render_q_term_v1(&output,
        binary(&arena, "q-app", unary(&arena, "q-sym", atom_symbol(&arena, "Pair")),
            binary(&arena, "q-cons", var, binary(&arena, "q-cons", var, nil))),
         error, sizeof error));
    assert(output.len == 14u && !memcmp(output.bytes, "(Pair ?v0 ?v0)", 14u));
    free(output.bytes);
    StateTransitionNumeralsV1 numerals = {0};
    assert(state_transition_collect_numerals(chain(&arena, atom_int(&arena, 7), 6000u),
        &numerals,  error, sizeof error));
    assert(numerals.len == 1u && numerals.values[0] == 7u); free(numerals.values);
    LangDefCanonicalAnswerV1 *literals = NULL; size_t literal_len = 0u, literal_cap = 0u;
    assert(state_occurrence_collect_literals_v1(chain(&arena,
        unary(&arena, "state-literal-v1", unary(&arena, "Payload", atom_int(&arena, 7))), 6000u),
        &literals, &literal_len, &literal_cap,  error, sizeof error));
    assert(literal_len == 1u && !strcmp(literals[0].text, "(Payload 7)"));
    free(literals[0].text); free(literals);
    StateOccurrenceRoleListRequestsV1 requests = {0};
    assert(state_occurrence_collect_requests_v1(chain(&arena,
        binary(&arena, "state-role-list-v1", atom_symbol(&arena, "role"), atom_int(&arena, 7)), 6000u),
        "op", &requests,  error, sizeof error));
    assert(requests.len == 1u && requests.values[0].index == 7u &&
        !strcmp(requests.values[0].role, "role") && !strcmp(requests.values[0].operation, "op"));
    free(requests.values[0].role); free(requests.values[0].operation); free(requests.values);
    Atom *cycle = binary(&arena, "q-cons", var, nil);
    cycle->expr.elems[2] = cycle;
    output = (LangDefByteBufferV1){0};
    assert(!langdef_semantic_render_q_term_v1(&output,
        binary(&arena, "q-app", box, cycle),  error, sizeof error));
    assert(!output.bytes && strstr(error, "cycle"));
    StateOccurrenceEmptyRolesV1 roles = {0};
    assert(!state_occurrence_parse_empty_roles_v1(cycle, &roles, error, sizeof error));
    cycle->expr.elems[2] = nil;
    arena_free(&arena);
    g_symbols = NULL; symbol_table_free(&symbols);
    puts("PASS: 6000-deep semantic quotation and state inventories preserve source structure; cycles refuse");
    return 0;
}
