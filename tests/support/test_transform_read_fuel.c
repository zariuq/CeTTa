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
    arena_free(&arena);
    engine_leave(engine, &scope);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    puts("PASS: 42 native read budgets distinguish exhaustion, empty answers, complete bags and unentered engine refusal");
    return 0;
}
