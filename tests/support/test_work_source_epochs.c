#include "eval.h"
#include "library_work.h"
#include "shared_transition.h"
#include "symbol.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static Atom *call(Arena *arena, const char *name, Atom **arguments, uint32_t count) {
    CettaCallOutcome result;
    assert(cetta_work_module_call(NULL, NULL, arena, atom_symbol(arena, name),
                                 arguments, count, &result));
    assert(result.kind == CETTA_CALL_VALUE);
    assert(result.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
    return result.term;
}

static Atom *capture(Arena *arena, Space *source) {
    Atom *arguments[] = {atom_space(arena, source)};
    return call(arena, "work:source-epoch", arguments, 1u);
}

static Atom *status(Arena *arena, Space *source, Atom *epoch) {
    Atom *arguments[] = {atom_space(arena, source), epoch};
    return call(arena, "work:source-status", arguments, 2u);
}

static void fault(Atom *value, const char *reason) {
    assert(value && value->kind == ATOM_EXPR && value->expr.len == 2u);
    assert(atom_is_symbol(value->expr.elems[0], "Work:Fault"));
    assert(atom_is_symbol(value->expr.elems[1], reason));
}

static void decimal(Atom *value, uint64_t expected) {
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, expected);
    assert(value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING);
    assert(atom_string_len(value) == strlen(text));
    assert(!memcmp(value->ground.sval, text, strlen(text)));
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena arena;
    arena_init_detached(&arena);
    Space root, sibling, overlay, nested, module;
    space_init(&root);
    space_init(&sibling);
    space_init(&module);
    Atom *row = atom_symbol(&arena, "source-row");
    space_add(&root, row);
    space_init_overlay(&overlay, &root);
    space_init_overlay(&nested, &overlay);
    Atom *epoch = capture(&arena, &root);
    assert(epoch->kind == ATOM_EXPR && epoch->expr.len == 4u);
    assert(atom_is_symbol(epoch->expr.elems[0], "Work:SourceEpoch"));
    decimal(epoch->expr.elems[1], space_instance_id(&root));
    decimal(epoch->expr.elems[2], space_revision(&root));
    decimal(epoch->expr.elems[3], space_read_token(&root).base_dependency_epoch);
    assert(atom_is_symbol(status(&arena, &root, epoch), "Work:Current"));
    assert(atom_is_symbol(status(&arena, &sibling, epoch), "Work:Changed"));
    assert(atom_is_symbol(status(&arena, &root, epoch), "Work:Current"));

    Atom *overlay_epoch = capture(&arena, &overlay);
    Atom *nested_epoch = capture(&arena, &nested);
    uint64_t overlay_revision = space_revision(&overlay);
    uint64_t nested_revision = space_revision(&nested);
    space_add(&root, atom_symbol(&arena, "outside-frozen-prefix"));
    assert(atom_is_symbol(status(&arena, &root, epoch), "Work:Changed"));
    assert(atom_is_symbol(status(&arena, &overlay, overlay_epoch), "Work:Current"));
    assert(atom_is_symbol(status(&arena, &nested, nested_epoch), "Work:Current"));
    assert(space_remove(&root, row));
    assert(space_revision(&overlay) == overlay_revision);
    assert(space_revision(&nested) == nested_revision);
    assert(atom_is_symbol(status(&arena, &overlay, overlay_epoch), "Work:Changed"));
    assert(atom_is_symbol(status(&arena, &nested, nested_epoch), "Work:Changed"));

    assert(space_add_dependency(&root, &module));
    epoch = capture(&arena, &root);
    overlay_epoch = capture(&arena, &overlay);
    nested_epoch = capture(&arena, &nested);
    space_add(&module, atom_symbol(&arena, "imported-row"));
    assert(atom_is_symbol(status(&arena, &root, epoch), "Work:Changed"));
    assert(atom_is_symbol(status(&arena, &overlay, overlay_epoch), "Work:Changed"));
    assert(atom_is_symbol(status(&arena, &nested, nested_epoch), "Work:Changed"));

    epoch = capture(&arena, &root);
    Atom *fields[] = {epoch->expr.elems[0], epoch->expr.elems[1],
                      epoch->expr.elems[2], epoch->expr.elems[3], atom_string(&arena, "extra")};
    fault(status(&arena, &root, atom_expr(&arena, fields, 5u)), "InvalidSourceEpoch");
    fault(status(&arena, &root, atom_expr(&arena, fields, 3u)), "InvalidSourceEpoch");
    fault(status(&arena, &root, atom_symbol(&arena, "unrelated")), "InvalidSourceEpoch");
    fields[1] = atom_int(&arena, (int64_t)space_instance_id(&root));
    assert(atom_is_symbol(status(&arena, &root, atom_expr(&arena, fields, 4u)), "Work:Changed"));
    fields[1] = atom_string(&arena, "01");
    assert(atom_is_symbol(status(&arena, &root, atom_expr(&arena, fields, 4u)), "Work:Changed"));
    fields[1] = atom_string_n(&arena, "1\0hidden", 8u);
    assert(atom_is_symbol(status(&arena, &root, atom_expr(&arena, fields, 4u)), "Work:Changed"));

    uint64_t saved_identity = root.instance_id;
    uint64_t saved_revision = root.revision;
    root.instance_id = UINT64_MAX - 1u;
    root.revision = UINT64_MAX - 2u;
    epoch = capture(&arena, &root);
    decimal(epoch->expr.elems[1], UINT64_MAX - 1u);
    decimal(epoch->expr.elems[2], UINT64_MAX - 2u);
    assert(atom_is_symbol(status(&arena, &root, epoch), "Work:Current"));
    root.instance_id = saved_identity;
    root.revision = saved_revision;
    assert(atom_is_symbol(status(&arena, &root, epoch), "Work:Changed"));

    cetta_shared_transition_scope_enter();
    epoch = capture(&arena, &nested);
    assert(!cetta_shared_transition_guard_held_by_current_thread());
    assert(atom_is_symbol(status(&arena, &nested, epoch), "Work:Current"));
    assert(!cetta_shared_transition_guard_held_by_current_thread());
    cetta_shared_transition_scope_leave();

    const Space *saved_base = overlay.overlay_base;
    overlay.overlay_base = &nested;
    fault(capture(&arena, &nested), "UnsupportedSourceView");
    overlay.overlay_base = saved_base;
    Atom *invalid[] = {atom_symbol(&arena, "not-a-source")};
    fault(call(&arena, "work:source-epoch", invalid, 1u), "InvalidSource");
    CettaCallOutcome unused;
    assert(!cetta_work_module_call(NULL, NULL, &arena, atom_symbol(&arena, "work:source-status"),
                                  invalid, 1u, &unused));
    space_free(&nested);
    fault(capture(&arena, &nested), "ClosedSource");
    fault(status(&arena, &nested, epoch), "ClosedSource");
    space_init(&nested);
    assert(atom_is_symbol(status(&arena, &nested, epoch), "Work:Changed"));

    space_free(&nested);
    space_free(&overlay);
    space_free(&root);
    space_free(&sibling);
    space_free(&module);
    arena_free(&arena);
    symbol_table_free(&symbols);
    puts("PASS: complete source epochs preserve lifetime, full uint64 fields, overlay and imported dependencies, protected reads and strict token shape");
    return 0;
}
