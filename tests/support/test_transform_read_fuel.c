#include "../../src/embedding.c"
#include "transform_core.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

typedef struct {
    Space *space;
    uint64_t remaining, spent;
    bool exhausted;
} Meter;

static bool resolve(void *context, Atom *reference, Space **space) {
    (void)reference;
    *space = ((Meter *)context)->space;
    return true;
}

static bool spend(void *context, uint64_t work) {
    Meter *meter = context;
    if (work > meter->remaining) {
        meter->exhausted = true;
        return false;
    }
    meter->remaining -= work;
    meter->spent += work;
    return true;
}

static void fresh_reads_release_temporary_terms(cetta_engine_t *engine) {
    Arena input;
    arena_init_detached(&input);
    Atom *reference = atom_symbol(&input, "&self");
    Atom *head = atom_symbol(&input, "fresh-row");
    Atom *row = atom_expr2(&engine->persistent,
        atom_symbol(&engine->persistent, "fresh-row"),
        atom_var_with_id(&engine->persistent, "stored", fresh_var_id()));
    space_add(&engine->session.space, row);
    Atom *pattern = atom_expr2(&input, head, atom_symbol(&input, "value"));
    Atom *value = atom_symbol(&input, "found");
    HashConsTable table;
    hashcons_init_compact(&table);
    HashConsTable *previous = g_hashcons;
    g_hashcons = &table;
    uint32_t warm = 0u;
    for (unsigned trial = 0u; trial <= 512u; trial++) {
        Arena output;
        arena_init_detached(&output);
        Meter meter = {&engine->session.space, 1000u, 0u, false};
        CettaTransformHost host = {.context = &meter,
            .dialect = CETTA_TRANSFORM_DIALECT_PETTA,
            .resolve_space = resolve, .spend = spend};
        Atom *answers = NULL;
        uint64_t count = 0u;
        assert(cetta_transform_reads(&host, &output, reference, pattern, value,
            CETTA_STEP_READS_ORDERED, &answers, &count) == CETTA_TRANSFORM_FIRED);
        assert(count == 1u && answers && answers->expr.len == 1u);
        assert(atom_eq(answers->expr.elems[0], value));
        arena_free(&output);
        if (!trial) warm = table.used;
        else assert(table.used == warm);
    }
    g_hashcons = previous;
    hashcons_free(&table);
    arena_free(&input);
    puts("PASS: repeated reads of a variable-bearing row do not retain fresh terms in the process interner");
}

int main(void) {
    cetta_engine_t *engine = NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
        "petta", NULL, NULL};
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    Arena arena;
    arena_init(&arena);
    Atom *reference = atom_symbol(&arena, "&self");
    Atom *pattern = atom_expr2(&arena, atom_symbol(&arena, "job"),
        atom_var_with_id(&arena, "x", fresh_var_id()));
    Atom *value = atom_symbol(&arena, "found");
    for (int rows = 0; rows <= 1; rows++) {
        if (rows) {
            Atom *fact = atom_expr2(&engine->persistent,
                atom_symbol(&engine->persistent, "job"),
                atom_symbol(&engine->persistent, "a"));
            space_add(&engine->session.space, fact);
        }
        for (int mode = CETTA_STEP_READS_ORDERED; mode <= CETTA_STEP_READS_ENGINE; mode++) {
            for (uint64_t budget = 0u; budget <= 6u; budget++) {
                Meter meter = {&engine->session.space, budget, 0u, false};
                CettaTransformHost host = {.context = &meter,
                    .dialect = CETTA_TRANSFORM_DIALECT_PETTA,
                    .resolve_space = resolve, .spend = spend};
                Atom *answers = NULL;
                uint64_t count = 0u;
                CettaTransformOutcome outcome = cetta_transform_reads(&host,
                    &arena, reference, pattern, value, (CettaStepReads)mode,
                    &answers, &count);
                assert(meter.spent <= budget);
                if (mode == CETTA_STEP_READS_ENGINE) {
                    assert(outcome == CETTA_TRANSFORM_DECLINED && meter.spent == 0u);
                } else if (budget < (rows ? 4u : 2u)) {
                    assert(outcome == CETTA_TRANSFORM_INTERRUPTED && meter.exhausted);
                    assert(!answers && count == 0u);
                } else {
                    assert(outcome == CETTA_TRANSFORM_FIRED && !meter.exhausted);
                    assert(answers && answers->expr.len == (CettaExprLen)rows);
                    assert(count == (uint64_t)rows);
                }
            }
        }
    }
    fresh_reads_release_temporary_terms(engine);
    arena_free(&arena);
    engine_leave(engine, &scope);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    puts("PASS: 42 native read budgets distinguish exhaustion, empty answers, complete bags and unentered engine refusal");
    return 0;
}
