#define CETTA_TEST_HOOKS 1

#include "eval.h"
#include "rhocalc_core.h"
#include "space.h"

#include <stdint.h>
#include <stdio.h>

static int fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    return 1;
}

static int test_registry_growth(void) {
    Registry registry;
    registry_init(&registry);
    for (uintptr_t i = 1; i <= 80; i++) {
        registry_bind_id(&registry, (SymbolId)i,
                         (Atom *)(uintptr_t)(0x1000u + i));
    }
    if (registry.len != 80)
        return fail("registry did not retain 80 entries");
    if (registry.cap < 80 || registry.entries == registry.inline_entries)
        return fail("registry did not spill from inline storage");
    for (uintptr_t i = 1; i <= 80; i++) {
        Atom *value = registry_lookup_id(&registry, (SymbolId)i);
        if (value != (Atom *)(uintptr_t)(0x1000u + i))
            return fail("registry lookup changed after spill");
    }
    registry_free(&registry);
    if (registry.len != 0 || registry.entries != registry.inline_entries)
        return fail("registry did not reset to inline storage");
    return 0;
}

static int test_eval_redirect_growth(void) {
    eval_payload_redirects_begin(NULL);
    for (uintptr_t i = 1; i <= 80; i++) {
        if (!eval_payload_note_space_redirect(
                (Space *)(i << 4), (Space *)((i + 1000u) << 4)))
            return fail("space redirect map refused entry past inline storage");
        if (!eval_payload_note_state_redirect(
                (StateCell *)(i << 4), (StateCell *)((i + 2000u) << 4)))
            return fail("state redirect map refused entry past inline storage");
    }
    if (eval_payload_space_redirect_count() != 80 ||
        eval_payload_state_redirect_count() != 80)
        return fail("redirect maps did not retain all entries");
    for (CettaCount i = 0; i < 80; i++) {
        Space *space_orig = NULL;
        Space *space_redirect = NULL;
        StateCell *state_orig = NULL;
        StateCell *state_redirect = NULL;
        if (!eval_payload_space_redirect_at(i, &space_orig, &space_redirect) ||
            space_orig != (Space *)(((uintptr_t)i + 1u) << 4) ||
            space_redirect != (Space *)(((uintptr_t)i + 1001u) << 4))
            return fail("space redirect lookup changed after spill");
        if (!eval_payload_state_redirect_at(i, &state_orig, &state_redirect) ||
            state_orig != (StateCell *)(((uintptr_t)i + 1u) << 4) ||
            state_redirect != (StateCell *)(((uintptr_t)i + 2001u) << 4))
            return fail("state redirect lookup changed after spill");
    }
    eval_payload_redirects_end();
    if (eval_payload_space_redirect_count() != 0 ||
        eval_payload_state_redirect_count() != 0)
        return fail("redirect maps did not reset after teardown");
    return 0;
}

int main(void) {
    if (test_registry_growth() != 0)
        return 1;
    if (test_eval_redirect_growth() != 0)
        return 1;
    if (!rhocalc_payload_map_capacity_selftest())
        return fail("rhocalc payload maps did not grow past inline storage");
    puts("PASS: rhometta payload map capacity C test");
    return 0;
}
