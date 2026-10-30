/* Inspect the private serializer independently of its hash bucket layout. */
#include "../../src/petta_program.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static unsigned checks;
static PettaPlanNode compared_metadata[512];
static size_t compared_metadata_count;
static bool deny_scratch_allocation;
static unsigned denied_scratch_allocations;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size) {
    if (deny_scratch_allocation) {
        denied_scratch_allocations++;
        return NULL;
    }
    return __real_malloc(size);
}
void *__real_realloc(void *, size_t);
void *__wrap_realloc(void *pointer, size_t size) {
    if (deny_scratch_allocation) {
        denied_scratch_allocations++;
        return NULL;
    }
    return __real_realloc(pointer, size);
}

static uint64_t reference_hash(const PettaPlanNode *node, uint64_t state) {
    uint64_t key[PETTA_PLAN_NODE_KEY_WORDS];
    petta_plan_node_key(node, key);
    for (size_t i = 0u; i < PETTA_PLAN_NODE_KEY_WORDS; i++) {
        state ^= key[i]; state *= UINT64_C(0x100000001b3); state ^= state >> 29;
    }
    for (CettaExprIndex i = 0u; i < node->child_count; i++)
        state = reference_hash(&node->children[i], state);
    return state;
}

static void check_scratch_worklists(void) {
    PettaPlanNode left[32] = {0}, right[32] = {0};
    PettaPlanNode left_branch[32] = {0}, right_branch[32] = {0};
    for (unsigned i = 0u; i < 32u; i++) {
        left[i].source_position = right[i].source_position = i + 1u;
        left_branch[i].source_position = right_branch[i].source_position = i + 33u;
    }
    left[31].children = left_branch; left[31].child_count = 32u;
    right[31].children = right_branch; right[31].child_count = 32u;
    left[0].children = left_branch; left[0].child_count = 32u;
    right[0].children = right_branch; right[0].child_count = 32u;
    PettaPlanNode first = {.children = left, .child_count = 32u};
    PettaPlanNode second = {.children = right, .child_count = 32u};
    uint64_t hash = 0u;
    assert(petta_plan_content_hash(&first, &hash));
    assert(hash == reference_hash(&first, UINT64_C(0xcbf29ce484222325)));
    assert(petta_plan_content_equal(&first, &second) == 1);
    right[0].source_debit = 1u;
    assert(petta_plan_content_equal(&first, &second) == 0);
    right[0].source_debit = 0u;
    deny_scratch_allocation = true;
    hash = UINT64_C(0x123456789);
    assert(!petta_plan_content_hash(&first, &hash));
    assert(petta_plan_content_equal(&first, &second) == -1);
    assert(denied_scratch_allocations == 2u);
    PettaPlanNode leaf = {0}, copy = leaf;
    assert(petta_plan_content_hash(&leaf, &hash));
    assert(petta_plan_content_equal(&leaf, &copy) == 1 && denied_scratch_allocations == 2u);
    copy.source_debit = 1u;
    assert(petta_plan_content_equal(&leaf, &copy) == 0);
    deny_scratch_allocation = false;
    checks += 9u;
}

static bool same_metadata(const PettaPlanNode *left, const PettaPlanNode *right) {
#define SAME(field) (left->field == right->field)
    return SAME(role) && SAME(execution) && SAME(continuation) && SAME(output) &&
        SAME(computation.control.operation) && SAME(computation.control.source) &&
        SAME(computation.control.first) && SAME(computation.control.last) &&
        SAME(computation.invocation.operation) && SAME(computation.invocation.first) &&
        SAME(computation.invocation.last) && SAME(output_child) &&
        SAME(contains_cardinality_call) && SAME(contains_call) &&
        SAME(contains_deferred_occurrence_transport) && SAME(plain_scalar_tree) &&
        SAME(relation_head_admitted) && SAME(open_template_admitted) &&
        SAME(has_equation_variable_slot) && SAME(dispatch_handler) &&
        SAME(literal_case_default) && SAME(plain_scalar_tree_operations) &&
        SAME(equation_variable_slot) && SAME(deterministic_region) &&
        SAME(region_hole_program) && SAME(child_count) &&
        SAME(source_position) && SAME(source_debit);
#undef SAME
}

static void compare_metadata(const PettaPlanNode *left, const PettaPlanNode *right) {
    uint64_t first[PETTA_PLAN_NODE_KEY_WORDS], second[PETTA_PLAN_NODE_KEY_WORDS];
    petta_plan_node_key(left, first);
    petta_plan_node_key(right, second);
    assert((memcmp(first, second, sizeof(first)) == 0) == same_metadata(left, right));
    checks++;
}

static void compare_variation(const PettaPlanNode *base, const PettaPlanNode *changed) {
    compare_metadata(base, changed);
    for (size_t i = 0u; i < compared_metadata_count; i++)
        compare_metadata(&compared_metadata[i], changed);
    assert(compared_metadata_count < sizeof(compared_metadata) / sizeof(*compared_metadata));
    compared_metadata[compared_metadata_count++] = *changed;
}

int main(void) {
    check_scratch_worklists();
    PettaPlanNode base = {0}, changed;
#define VARY(field, value) do { changed = base; changed.field = (value); \
    assert(!same_metadata(&base, &changed)); compare_variation(&base, &changed); } while (0)
#define BYTE(field) do { for (unsigned bit = 0u; bit < 8u; bit++) { \
    VARY(field, UINT8_C(1) << bit); } VARY(field, UINT8_MAX); } while (0)
#define WORD(field) do { for (unsigned bit = 0u; bit < 32u; bit++) { \
    VARY(field, UINT32_C(1) << bit); } VARY(field, UINT32_MAX); } while (0)
    BYTE(role); BYTE(execution); BYTE(continuation); BYTE(output);
    BYTE(computation.control.operation); BYTE(computation.control.source);
    BYTE(computation.control.first); BYTE(computation.control.last);
    BYTE(computation.invocation.operation); BYTE(computation.invocation.first);
    BYTE(computation.invocation.last);
    WORD(plain_scalar_tree_operations); WORD(equation_variable_slot);
    WORD(source_position); WORD(source_debit);
    for (unsigned bit = 0u; bit < 64u; bit++) {
        VARY(output_child, UINT64_C(1) << bit);
        VARY(child_count, UINT64_C(1) << bit);
    }
    VARY(output_child, UINT64_MAX); VARY(child_count, UINT64_MAX);
    VARY(contains_cardinality_call, true); VARY(contains_call, true);
    VARY(contains_deferred_occurrence_transport, true); VARY(plain_scalar_tree, true);
    VARY(relation_head_admitted, true); VARY(open_template_admitted, true);
    VARY(has_equation_variable_slot, true); VARY(dispatch_handler, true);
    VARY(literal_case_default, true);
    Arena owners;
    arena_init(&owners);
    VARY(deterministic_region, arena_alloc(&owners, 64u));
    VARY(region_hole_program, arena_alloc(&owners, 64u));
    compare_metadata(&base, &base);
    base = (PettaPlanNode){.role = UINT8_MAX, .execution = UINT8_MAX,
        .continuation = UINT8_MAX, .output = UINT8_MAX,
        .computation = {.control = {UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX},
            .invocation = {UINT8_MAX, UINT8_MAX, UINT8_MAX}},
        .source_position = UINT32_MAX, .source_debit = UINT32_MAX,
        .plain_scalar_tree_operations = UINT32_MAX, .equation_variable_slot = UINT32_MAX};
    compare_metadata(&base, &base);
    VARY(continuation, 0u); VARY(output, 0u);
    VARY(computation.invocation.operation, 0u); VARY(computation.control.operation, 0u);
    VARY(plain_scalar_tree_operations, 0u); VARY(equation_variable_slot, 0u);
    VARY(source_position, 0u); VARY(source_debit, 0u);
#undef WORD
#undef BYTE
#undef VARY
    PettaProgram *program = petta_program_new();
    assert(program);
    PettaPlanNode leaf = {.role = PETTA_PLAN_VALUE,
        .computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN,
        .source_position = 1u, .source_debit = 1u};
    const PettaPlanNode *first = petta_program_intern_plan(program, &leaf, false);
    const PettaPlanNode *again = petta_program_intern_plan(program, &leaf, false);
    assert(first && again == first);
    leaf.source_position = 2u;
    const PettaPlanNode *other = petta_program_intern_plan(program, &leaf, false);
    assert(other && other != first);
    leaf.source_position = 1u; leaf.source_debit = 0u;
    assert(petta_program_intern_plan(program, &leaf, false) != first);
    PettaPlanNode forward[] = {*first, *other}, backward[] = {*other, *first};
    PettaPlanNode left = {.child_count = 2u, .children = forward};
    PettaPlanNode right = {.child_count = 2u, .children = backward};
    assert(petta_plan_content_equal(&left, &left) == 1 &&
        petta_plan_content_equal(&left, &right) == 0);
    checks += 5u;
    /* Force a fingerprint collision at the lookup boundary. Its candidate
     * must still undergo exact comparison before a plan is reused. */
    PettaPlanNode collided = *first;
    collided.source_position = 3u;
    uint64_t collision_hash;
    assert(petta_plan_content_hash(&collided, &collision_hash));
    size_t original_slot = 0u;
    while (program->interned_plans[original_slot].plan != first) {
        assert(++original_slot < program->interned_plan_cap);
    }
    PettaPlanInternEntry forced = program->interned_plans[original_slot];
    program->interned_plans[original_slot] = (PettaPlanInternEntry){0};
    forced.hash = collision_hash;
    size_t forced_slot = (size_t)collision_hash & (program->interned_plan_cap - 1u);
    while (program->interned_plans[forced_slot].plan)
        forced_slot = (forced_slot + 1u) & (program->interned_plan_cap - 1u);
    program->interned_plans[forced_slot] = forced;
    const PettaPlanNode *separate = petta_program_intern_plan(program, &collided, false);
    assert(separate && separate != first && separate->source_position == 3u);
    checks++;
    petta_program_free(program);
    arena_free(&owners);
    printf("PASS: %u plan-key controls preserve all fields, source charges and ordered children\n", checks);
    return 0;
}
