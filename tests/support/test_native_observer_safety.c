#include "eval.h"
#include "library.h"
#include "library_binary.h"
#include "parser.h"
#include "petta_program.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Atom *read_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    assert(count == 1 && forms);
    Atom *value = forms[0];
    free(forms);
    return value;
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
    Space space;
    space_init(&space);
    CettaLibraryContext library;
    cetta_library_context_init_for_language_profile(&library, CETTA_LANGUAGE_PETTA, NULL);
    CettaLibraryContext *previous = eval_current_library_context();
    eval_set_library_context(&library);
    PettaProgram *program = petta_program_new();
    assert(program);
    const char *equations[] = {
        "(= (observer-unsigned $x) (binary:unsigned? $x))",
        "(= (observer-kind $x) (binary:value-kind $x))",
        "(= (observer-portable-kind $x) (binary:portable-kind $x))",
        "(= (observer-wrong-arity $x) (binary:unsigned? $x 0))",
        "(= (observer-partial $x) (binary:pack-fields $x))",
        "(= (observer-construct $x) (cons-atom $x ()))",
        "(= (observer-deconstruct $x) (decons-atom $x))",
        "(= (observer-concatenate $x) (union-atom $x ()))",
        "(= (observer-effect $x) (cons-atom (println! $x) ()))",
        "(= (observer-pattern $x) (let ($head $tail) (decons-atom $x) $head))",
        "(= (observer-chain $x) (chain (decons-atom $x) ($head $tail) $head))",
        "(= (observer-binding-effect $x) (let ($head $tail) (decons-atom $x) (println! $head)))",
        "(= (observer-binding-source-effect $x) (let ($head $tail) (println! $x) $head))",
    };
    for (size_t i = 0; i < sizeof(equations) / sizeof(*equations); i++)
        space_add(&space, read_one(&arena, equations[i]));
    assert(petta_program_synchronize_space(program, &space));
    SymbolId unsigned_head = atom_symbol_current_id(atom_symbol(&arena, "observer-unsigned"));
    SymbolId kind_head = atom_symbol_current_id(atom_symbol(&arena, "observer-kind"));
    SymbolId portable_head = atom_symbol_current_id(atom_symbol(&arena, "observer-portable-kind"));
    SpaceProgramToken unchanged = space_program_token(&space);

    /* Availability changes without editing the program. Both stale unsafe
     * and stale safe cache answers must be rejected under the new basis. */
    library.active_mask = 0u;
    assert(petta_program_relation_safety(program, &space, unsigned_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    assert(petta_program_relation_safety(program, &space, kind_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    assert(petta_program_relation_safety(program, &space, portable_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    library.active_mask = CETTA_NATIVE_IMPORT_BINARY;
    assert(petta_program_relation_safety(program, &space, unsigned_head, 1u) == PETTA_RELATION_SAFETY_STATIC);
    assert(petta_program_relation_safety(program, &space, kind_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    assert(petta_program_relation_safety(program, &space, portable_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    assert(petta_program_relation_safety(program, &space, unsigned_head, 1u) == PETTA_RELATION_SAFETY_STATIC);
    library.active_mask = 0u;
    assert(petta_program_relation_safety(program, &space, unsigned_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    assert(petta_program_relation_safety(program, &space, kind_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    assert(space_program_token_eq(unchanged, space_program_token(&space)));
    CettaLoadedModule module = {0};
    snprintf(module.display_name, sizeof(module.display_name), "binary");
    CettaLoadedModule *loaded[] = {&module};
    CettaLoadedModule **saved_loaded = library.loaded_modules;
    uint32_t saved_length = library.loaded_module_len;
    library.loaded_modules = loaded;
    library.loaded_module_len = 1u;
    assert(eval_current_native_observer_mask() == CETTA_NATIVE_IMPORT_BINARY);
    assert(petta_program_relation_safety(program, &space, unsigned_head, 1u) == PETTA_RELATION_SAFETY_STATIC);
    library.loaded_modules = saved_loaded;
    library.loaded_module_len = saved_length;
    assert(petta_program_relation_safety(program, &space, unsigned_head, 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    library.active_mask = CETTA_NATIVE_IMPORT_BINARY;
    const char *refused[] = {"observer-wrong-arity", "observer-partial", "observer-effect",
        "observer-binding-effect", "observer-binding-source-effect"};
    for (size_t i = 0; i < sizeof(refused) / sizeof(*refused); i++)
        assert(petta_program_relation_safety(program, &space,
            atom_symbol_current_id(atom_symbol(&arena, refused[i])), 1u) == PETTA_RELATION_SAFETY_UNSAFE);
    const char *structural[] = {"observer-construct", "observer-deconstruct", "observer-concatenate",
        "observer-pattern", "observer-chain"};
    for (size_t i = 0; i < sizeof(structural) / sizeof(*structural); i++)
        assert(petta_program_relation_safety(program, &space,
            atom_symbol_current_id(atom_symbol(&arena, structural[i])), 1u) == PETTA_RELATION_SAFETY_STATIC);

    petta_program_free(program);
    eval_set_library_context(previous);
    cetta_library_context_free(&library);
    space_free(&space);
    arena_free(&arena);
    symbol_table_free(&symbols);
    puts("PASS: native observers, structural constructors, pattern binders, refusal and availability-keyed safety cache (23 controls)");
    return 0;
}
