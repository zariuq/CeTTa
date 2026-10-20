#include "answer_storage.h"
#include "symbol.h"
#include "variant_shape.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    CettaForeignHold hold;
    int shares;
} ForeignProbe;

static void retain_foreign(void *opaque) {
    ((ForeignProbe *)opaque)->shares++;
}

static void release_foreign(void *opaque) {
    ((ForeignProbe *)opaque)->shares--;
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    CettaAnswerStorageAccount *account = NULL;
    CettaAnswerStorage *storage = cetta_answer_storage_new(&account);
    CettaAnswerStorage *other = cetta_answer_storage_new(&account);
    assert(storage && other && account->shares == 3u);
    assert(!cetta_answer_storage_account_retain(NULL));
    CettaAnswerStorageAccount *replacement_account = account;
    assert(cetta_answer_storage_account_retain(replacement_account));
    assert(account->shares == 4u);
    Atom *value = atom_int(&storage->arena, 41);
    Atom *record = atom_expr(&storage->arena, (Atom *[]){value, value}, 2u);
    assert(cetta_answer_storage_can_borrow(storage, record));
    assert(!cetta_answer_storage_can_borrow(other, record));
    Atom *other_value = atom_int(&other->arena, 29);
    Atom *mixed = atom_expr(&storage->arena, (Atom *[]){value, other_value}, 2u);
    assert(!cetta_answer_storage_can_borrow(storage, mixed));
    Atom *variable = atom_var(&storage->arena, "local");
    Atom *open = atom_expr(&storage->arena, (Atom *[]){variable, variable}, 2u);
    assert(!cetta_answer_storage_can_borrow(storage, open));
    ForeignProbe foreign = {
        .hold = {.retain = retain_foreign, .release = release_foreign},
        .shares = 1,
    };
    Atom *opaque = atom_foreign(&storage->arena, (CettaForeignValue *)&foreign);
    assert(opaque && foreign.shares == 2);
    assert(!cetta_answer_storage_can_borrow(storage, opaque));
    assert(!cetta_answer_storage_can_borrow(NULL, record));
    assert(!cetta_answer_storage_can_borrow(storage, NULL));

    Arena first, second;
    arena_init(&first);
    arena_init(&second);
    CettaVarMap source_slots, goal_slots, first_locals, second_locals;
    cetta_var_map_init(&source_slots);
    cetta_var_map_init(&goal_slots);
    cetta_var_map_init(&first_locals);
    cetta_var_map_init(&second_locals);
    const CettaVariantShapeOptions options = {
        .slot_policy = CETTA_VARIANT_SLOT_ORDINAL_NAME,
        .slot_name = "$P",
        .share_immutable = false,
    };
    Atom *canonical = variant_shape_canonicalize_atom(
        &storage->arena, open, &source_slots, NULL, &options);
    Atom *one = variant_shape_materialize_atom(&first, canonical, &goal_slots, &first_locals);
    Atom *two = variant_shape_materialize_atom(&second, canonical, &goal_slots, &second_locals);
    assert(one && two && var_id_eq(one->expr.elems[0]->var_id, one->expr.elems[1]->var_id));
    assert(var_id_eq(two->expr.elems[0]->var_id, two->expr.elems[1]->var_id));
    assert(!var_id_eq(one->expr.elems[0]->var_id, two->expr.elems[0]->var_id));
    cetta_var_map_free(&source_slots);
    cetta_var_map_free(&goal_slots);
    cetta_var_map_free(&first_locals);
    cetta_var_map_free(&second_locals);

    CettaAnswerStorageUsage before, retained, released;
    assert(cetta_answer_storage_account_usage(account, &before));
    assert(before.collections == 2u);
    assert(cetta_answer_storage_retain_into(storage, &first));
    assert(cetta_answer_storage_retain_into(storage, &second));
    assert(cetta_answer_storage_retain_into(storage, &first));
    assert(storage->shares == 3u);
    assert(cetta_answer_storage_account_usage(account, &retained));
    assert(retained.control_bytes == before.control_bytes);
    assert(retained.arena_live_bytes == before.arena_live_bytes);
    assert(retained.arena_reserved_bytes == before.arena_reserved_bytes);
    cetta_answer_storage_release(other);
    cetta_answer_storage_release(storage);
    cetta_answer_storage_account_release(account);
    account = replacement_account;
    assert(storage->shares == 2u);
    assert(cetta_answer_storage_account_usage(account, &retained));
    assert(retained.collections == 1u);
    assert(record->expr.elems[0]->ground.ival == 41);
    arena_free(&first);
    assert(storage->shares == 1u && foreign.shares == 2);
    assert(record->expr.elems[1]->ground.ival == 41);
    arena_free(&second);
    assert(foreign.shares == 1);
    assert(cetta_answer_storage_account_usage(account, &released));
    assert(released.collections == 0u && released.arena_live_bytes == 0u);
    assert(released.arena_reserved_bytes == 0u);
    cetta_answer_storage_account_release(account);

    /* The collection's own account share also protects it when its store
     * handle is retired before the final receiver releases it. */
    account = NULL;
    storage = cetta_answer_storage_new(&account);
    arena_init(&first);
    assert(cetta_answer_storage_retain_into(storage, &first));
    cetta_answer_storage_release(storage);
    cetta_answer_storage_account_release(account);
    assert(storage->shares == 1u);
    arena_free(&first);

    var_intern_free(&variables);
    symbol_table_free(&symbols);
    puts("PASS: immutable ground answer admission and independent variable freshening");
    puts("PASS: retired answer storage survives independent cancellation and is counted once");
    puts("PASS: final receiver releases storage, resources and a retired account");
    return 0;
}
