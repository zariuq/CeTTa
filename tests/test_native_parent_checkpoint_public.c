/* Public capability refusal leaves its arguments and ordinary evaluation intact. */
#include "eval.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Atom *parse(Arena *a,const char *s){Atom **items=NULL;int n=parse_metta_text(s,a,&items);assert(n==1);Atom *r=items[0];free(items);return r;}
int main(void) {
 SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
 VarInternTable vars;var_intern_init(&vars);g_var_intern=&vars;
 Arena arena;arena_init(&arena);Space space;space_init(&space);
 CettaLibraryContext *context=malloc(sizeof(*context));assert(context);
 cetta_library_context_init_for_language_profile(context,CETTA_LANGUAGE_PRIME,cetta_profile_prime_default());eval_set_library_context(context);
 Atom *sentinel=atom_int(&arena,7),*out=sentinel;CettaContinuationStatus status=CETTA_CONTINUATION_CAPACITY;
 assert(!cetta_prime_checkpoint_copy_new(context,sentinel,&status));assert(status==CETTA_CONTINUATION_UNSUPPORTED);
 assert(!cetta_prime_checkpoint_copy_new_reserved(context,sentinel,&status));assert(status==CETTA_CONTINUATION_UNSUPPORTED);
 assert(cetta_prime_checkpoint_copy_replace(NULL,NULL)==CETTA_CONTINUATION_UNSUPPORTED);
 AtomDeepCopyArenaPair pair={&arena,&arena};
 assert(!cetta_prime_checkpoint_copy_arena_pair(NULL,&pair));assert(pair.source==&arena&&pair.destination==&arena);
 assert(cetta_prime_checkpoint_copy_prepare(NULL,NULL)==CETTA_CONTINUATION_UNSUPPORTED);
 assert(cetta_prime_checkpoint_copy_publish(NULL,&arena,&out)==CETTA_CONTINUATION_UNSUPPORTED);assert(out==sentinel);
 assert(!strcmp(cetta_prime_checkpoint_copy_refusal(NULL),"NoCheckpointCopy"));cetta_prime_checkpoint_copy_free(NULL);
 space_add(&space,parse(&arena,"(= (ordinary-checkpoint-id $x) $x)"));
 ResultSet result;result_set_init(&result);
 metta_eval(&space,&arena,atom_symbol(&arena,"%Undefined%"),parse(&arena,"(ordinary-checkpoint-id 7)"),128,&result);
 assert(result.len==1&&atom_eq(result.items[0],sentinel));result_set_free(&result);
 cetta_library_context_free(context);free(context);eval_set_library_context(NULL);space_free(&space);arena_free(&arena);
 eval_match_decision_cache_free_for_current_thread();var_intern_free(&vars);g_var_intern=NULL;symbol_table_free(&symbols);g_symbols=NULL;
 puts("PASS public checkpoint refusal and ordinary evaluation");return 0;
}
