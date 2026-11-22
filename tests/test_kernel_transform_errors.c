#include "atom.h"
#include "match.h"
#include "transform_core.h"
#include "tests/test_runtime_stats_stubs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);
    g_symbols=&symbols;g_hashcons=NULL;
    VarInternTable variables;var_intern_init(&variables);g_var_intern=&variables;
    Arena owner;arena_init(&owner);
    for (unsigned form=0;form<2;++form) {
        Arena source;arena_init_detached(&source);
        Atom *invalid=atom_expr2(&source,atom_symbol(&source,"take"),atom_symbol(&source,"unused"));
        if (!form) invalid=atom_expr(&source,invalid->expr.elems,1u);
        else { invalid->expr.elems[0]=atom_symbol(&source,"let"); }
        Atom *parts[]={atom_symbol(&source,"tf:run"),atom_int(&source,42),invalid,
            atom_symbol(&source,"answer"),atom_symbol(&source,"tf:caller")};
        CettaTransformHost host={.dialect=CETTA_TRANSFORM_DIALECT_HE};
        Atom *answer=cetta_transform_lower(&owner,atom_expr(&source,parts,5u),&host);
        assert(answer);arena_free(&source);
        /* Reuse released scratch blocks before observing any returned field. */
        for (unsigned round=0;round<8;++round) {
            Arena churn;arena_init_detached(&churn);
            for (unsigned n=0;n<256;++n) (void)atom_expr2(&churn,atom_int(&churn,n),atom_int(&churn,round));
            arena_free(&churn);
        }
        assert(answer->kind==ATOM_EXPR && answer->expr.len==3u);
        assert(atom_is_symbol(answer->expr.elems[0],"Error"));
        assert(atom_is_symbol(answer->expr.elems[2],"MalformedTransformSource"));
        Atom *site=answer->expr.elems[1];assert(site && site->kind==ATOM_EXPR && site->expr.len==(form ? 2u : 1u));
        assert(atom_is_symbol(site->expr.elems[0],form ? "let" : "take"));
    }
    arena_free(&owner);bindings_thread_cache_free();g_var_intern=NULL;var_intern_free(&variables);
    g_symbols=NULL;symbol_table_free(&symbols);
    puts("PASS: transform preparation errors retain complete caller-owned values after source and scratch reclamation");
}
