#include "atom.h"
#include "match.h"
#include "stats.h"
#include "tests/test_runtime_stats_stubs.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c, m) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL: %s\n",m); } } while (0)

static void exercise(Arena *a, bool epoch) {
    Atom *f = atom_symbol(a,"frame-test-f");
    Atom *x = atom_var_with_id(a,"same-spelling",UINT64_C(10001));
    Atom *y = atom_var_with_id(a,"same-spelling",UINT64_C(10002));
    Atom *u = atom_symbol(a,"frame-test-u");
    Atom *v = atom_symbol(a,"frame-test-v");
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb,NULL),"initialize binding builder");
    #define RUN(l,r) (epoch ? match_atoms_epoch_view_builder_current((l),0u,0u,(r),&bb,a) : match_atoms_builder((l),(r),&bb))
    Atom *left=atom_expr3(a,f,u,atom_expr2(a,f,u));
    Atom *right=atom_expr3(a,f,u,atom_expr2(a,f,u));
    CHECK(RUN(left,right),"equal rigid leaves succeed in nested expressions");
    CHECK(bb.current.len==0u,"literal matching does not write bindings");
    CHECK(!RUN(atom_expr2(a,f,u),atom_expr2(a,f,v)),"unequal symbols still fail");
    CHECK(!RUN(atom_expr2(a,f,u),atom_expr2(a,v,u)),"the first child is still checked");
    CHECK(RUN(atom_expr2(a,x,u),atom_expr2(a,f,u)),"variable first child is not skipped");
    CHECK(bindings_lookup_id(&bb.current,x->var_id)==f,"first-child binding is retained");
    bindings_builder_rollback(&bb,0u);
    CHECK(RUN(atom_expr(a,NULL,0u),atom_expr(a,NULL,0u)),"empty expressions have no first child");

    /* A later mismatch must not be hoisted ahead of earlier bindings. */
    CHECK(!RUN(atom_expr3(a,f,x,u),atom_expr3(a,f,v,v)),"later literal mismatch rejects");
    CHECK(bindings_lookup_id(&bb.current,x->var_id)==v,"failure retains exactly the earlier binding prefix");
    bindings_builder_rollback(&bb,0u);
    CHECK(bb.current.len==0u,"caller rollback restores entry");
    CHECK(!RUN(atom_expr3(a,f,u,x),atom_expr3(a,f,v,u)),"early literal mismatch rejects");
    CHECK(bb.current.len==0u,"later variable is not bound after early mismatch");
    CHECK(!RUN(atom_expr2(a,atom_int(a,1),x),atom_expr2(a,atom_int(a,2),u)),"grounded first-child mismatch still rejects");
    CHECK(bb.current.len==0u,"grounded mismatch precedes later bindings");

    CHECK(RUN(atom_expr2(a,f,x),atom_expr2(a,f,y)),"equal-spelled distinct variables still unify");
    Atom *mapped=bindings_lookup_id(&bb.current,x->var_id);
    CHECK(mapped && mapped->kind==ATOM_VAR && mapped->var_id==y->var_id,"variable aliases are not mistaken for equal symbols");
    bindings_builder_rollback(&bb,0u);
    CHECK(!RUN(atom_expr3(a,f,x,x),atom_expr3(a,f,u,v)),"repeated variable mismatch rejects");
    CHECK(bindings_lookup_id(&bb.current,x->var_id)==u,"repeated-variable failure keeps original orientation");
    bindings_builder_rollback(&bb,0u);
    CHECK(RUN(atom_expr3(a,f,x,x),atom_expr3(a,f,u,u)),"repeated variable agreement succeeds");
    CHECK(bb.current.len==1u,"repeated occurrence does not duplicate binding");
    bindings_builder_rollback(&bb,0u);
    /* This API may record a cyclic substitution; its caller owns the
     * bindings_has_loop guard as well as rollback. Test that actual contract. */
    bool cycle_matched=RUN(x,atom_expr2(a,f,x));
    CHECK(!cycle_matched || bindings_has_loop(&bb.current),"cyclic substitution remains visible to caller's occurs check");
    bindings_builder_rollback(&bb,0u);
    CHECK(!bindings_has_loop(&bb.current),"rollback restores cycle state");

    Atom *shared=atom_expr2(a,f,u);
    CHECK(RUN(atom_expr3(a,f,shared,shared),atom_expr3(a,f,shared,shared)),"shared finite DAG may re-enter through sibling paths");
    Atom *cycle=atom_expr2(a,f,u);
    cycle->expr.elems[1]=cycle;
    CHECK(!RUN(cycle,cycle),"cyclic raw expression still fails active-path audit");
    cycle->expr.elems[1]=u;
    cycle->expr.elems[0]=cycle;
    CHECK(!RUN(cycle,cycle),"first-child raw cycle still fails active-path audit");
    cycle->expr.elems[0]=f;

    /* Both original and fused worklists must handle heap-spilled frontiers. */
    Atom *ls[80], *rs[80];
    for (unsigned i=0;i<80u;i++) {
        ls[i]=(i&1u) ? atom_var_with_id(a,"wide-slot",UINT64_C(20000)+i) : u;
        rs[i]=(i&1u) ? v : u;
    }
    CHECK(RUN(atom_expr(a,ls,80u),atom_expr(a,rs,80u)),"wide mixed frontier succeeds");
    CHECK(bb.current.len==40u,"wide frontier preserves all variable bindings");
    bool all_bound=true;
    for (unsigned i=1;i<80u;i+=2u)
        all_bound=all_bound && bindings_lookup_id(&bb.current,ls[i]->var_id)==v;
    CHECK(all_bound,"wide frontier binds the correct identities");
    bindings_builder_rollback(&bb,0u);
    CHECK(bb.current.len==0u,"wide frontier rollback restores entry");
    Atom *deep_left=u, *deep_right=u;
    for (unsigned i=0;i<100u;i++) {
        deep_left=atom_expr2(a,f,deep_left);
        deep_right=atom_expr2(a,f,deep_right);
    }
    CHECK(RUN(deep_left,deep_right),"deep exit markers and active-path spill remain balanced");
    bindings_builder_free(&bb);
    #undef RUN
}

static void epoch_separation(Arena *a) {
    Atom *f=atom_symbol(a,"epoch-f");
    Atom *x=atom_var_with_id(a,"epoch-variable",UINT64_C(30001));
    Atom *term=atom_expr2(a,f,x);
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb,NULL),"initialize epoch separation builder");
    CHECK(match_atoms_epoch_view_builder(term,21u,0u,term,&bb,a,22u),
          "one source variable in distinct activations can unify");
    VarId l=var_epoch_id(x->var_id,21u), r=var_epoch_id(x->var_id,22u);
    Atom *lm=bindings_lookup_id(&bb.current,l), *rm=bindings_lookup_id(&bb.current,r);
    CHECK(bb.current.len==1u &&
          ((lm && lm->kind==ATOM_VAR && lm->var_id==r) ||
           (rm && rm->kind==ATOM_VAR && rm->var_id==l)),
          "equal source pointers do not erase distinct activation identities");
    bindings_builder_rollback(&bb,0u);
    term=atom_expr2(a,x,f);
    CHECK(match_atoms_epoch_view_builder(term,21u,0u,term,&bb,a,22u),"variable heads retain source activations");
    lm=bindings_lookup_id(&bb.current,l); rm=bindings_lookup_id(&bb.current,r);
    CHECK(bb.current.len==1u &&
          ((lm && lm->kind==ATOM_VAR && lm->var_id==r) ||
           (rm && rm->kind==ATOM_VAR && rm->var_id==l)),
          "first-child fusion preserves both epoch flags");
    bindings_builder_free(&bb);
}

static void plan_validation(Arena *a) {
    Atom *f=atom_symbol(a,"plan-f"), *u=atom_symbol(a,"plan-u");
    Atom *left=atom_expr2(a,f,u), *right=atom_expr2(a,f,u);
    CettaOpenPatternPlan children[2] = {
      {.source=f,.kind=ATOM_SYMBOL}, {.source=u,.kind=ATOM_SYMBOL}
    };
    CettaOpenPatternPlan root={.source=right,.kind=ATOM_EXPR,.children=children,.child_count=2u};
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb,NULL),"initialize planned builder");
    CHECK(match_atoms_epoch_builder_rule_local_planned(left,right,&root,&bb,a,7u),"valid source plans permit first-child frame fusion");
    children[0].source=u;
    CHECK(!match_atoms_epoch_builder_rule_local_planned(left,right,&root,&bb,a,7u),"wrong first-child source is rejected");
    children[0].source=f; children[0].kind=ATOM_VAR;
    CHECK(!match_atoms_epoch_builder_rule_local_planned(left,right,&root,&bb,a,7u),"wrong first-child kind is rejected");
    children[0].kind=ATOM_SYMBOL;
    children[1].source=f;
    CHECK(!match_atoms_epoch_builder_rule_local_planned(left,right,&root,&bb,a,7u),"wrong later-child source is rejected");
    children[1].source=u; children[1].kind=ATOM_VAR;
    CHECK(!match_atoms_epoch_builder_rule_local_planned(left,right,&root,&bb,a,7u),"wrong later-child kind is rejected");
    CHECK(bb.current.len==0u,"malformed literal-only plans do not add bindings");
    bindings_builder_free(&bb);
}

static void test_nested_head_dereferences(Arena *a) {
    Atom *f = atom_symbol(a, "head-frame");
    Atom *leaf = atom_symbol(a, "head-leaf");
    Atom *v = atom_var_with_id(a, "head-ref", UINT64_C(999001));
    Atom *left = leaf, *right = leaf;
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb, NULL), "head-depth builder");
    for (unsigned i = 0u; i < 100u; ++i) {
        left = atom_expr2(a, left, f);
        right = atom_expr2(a, right, f);
    }
    CHECK(bindings_builder_add_var_fresh(&bb, v, left), "head-depth binding");
    CHECK(match_atoms_builder(v, right, &bb), "nested expression heads preserve pair boundaries");
    bindings_builder_free(&bb);
}

static void test_type_wildcards(Arena *a) {
    Atom *wildcard = atom_symbol(a, "%Undefined%");
    Atom *f = atom_symbol(a, "type-frame");
    Atom *u = atom_symbol(a, "type-u");
    Atom *v = atom_symbol(a, "type-v");
    Atom *x = atom_var_with_id(a, "type-slot", UINT64_C(999002));
    Bindings bindings;
    bindings_init(&bindings);
    CHECK(match_types(atom_expr2(a, u, v),
                      atom_expr2(a, wildcard, wildcard), &bindings),
          "nested type wildcards remain independent in the plain bindings API");
    CHECK(!match_types(atom_expr2(a, wildcard, u),
                       atom_expr2(a, f, v), &bindings),
          "first-child wildcard does not suppress a later type mismatch");
    CHECK(!match_types(atom_expr3(a, x, u, wildcard),
                       atom_expr3(a, f, v, u), &bindings),
          "type mismatch retains the earlier ordinary variable binding");
    CHECK(bindings_lookup_id(&bindings, x->var_id) == f,
          "plain bindings retain the same failure prefix as the builder");
    bindings_free(&bindings);
}

static void test_unary_spine(Arena *a) {
    Atom *u = atom_symbol(a, "spine-u");
    Atom *v = atom_symbol(a, "spine-v");
    Atom *x = atom_var_with_id(a, "spine-slot", UINT64_C(999003));
    Atom *left = x, *right = u, *mismatch = v;
    for (unsigned i = 0u; i < 4096u; i++) {
        left = atom_expr(a, &left, 1u);
        right = atom_expr(a, &right, 1u);
        mismatch = atom_expr(a, &mismatch, 1u);
    }
    for (unsigned epoch = 0u; epoch < 2u; epoch++) {
        BindingsBuilder builder;
        CHECK(bindings_builder_init(&builder, NULL), "unary spine builder");
        bool matched = epoch
            ? match_atoms_epoch_view_builder_current(
                  left, 0u, 0u, right, &builder, a)
            : match_atoms_builder(left, right, &builder);
        CHECK(matched && bindings_lookup_id(&builder.current, x->var_id) == u,
              "a deep unary spine reaches and binds its leaf without recursion");
        matched = epoch
            ? match_atoms_epoch_view_builder_current(
                  left, 0u, 0u, mismatch, &builder, a)
            : match_atoms_builder(left, mismatch, &builder);
        CHECK(!matched && builder.current.len == 1u,
              "a prebound leaf mismatch keeps its entry bindings");
        bindings_builder_rollback(&builder, 0u);
        CHECK(builder.current.len == 0u, "unary spine caller rollback");
        bindings_builder_free(&builder);
    }
}

static void test_epoch_binding_keys(void) {
    for (unsigned structured = 0u; structured < 2u; structured++) {
        for (unsigned right_key = 0u; right_key < 2u; right_key++) {
            Arena source, target, expected_arena;
            arena_init(&source); arena_init(&target); arena_init(&expected_arena);
            Atom *name = structured
                ? atom_expr2(&source, atom_symbol(&source, "key-name"),
                             atom_int(&source, 17)) : NULL;
            Atom *variable = atom_var_with_presentation(
                &source, atom_symbol(&source, "epoch-key")->sym_id, name,
                UINT64_C(91001));
            VarId key = var_epoch_id(variable->var_id, 31u);
            Atom *expected_key = atom_var_like(&expected_arena, variable, key);
            Atom *value = atom_int(&target, 42);
            BindingsBuilder builder;
            CHECK(bindings_builder_init(&builder, NULL), "epoch key builder");
            size_t before = arena_accounted_live_bytes(&target);
            bool matched = right_key
                ? match_atoms_epoch_view_builder(
                      value, 0u, 0u, variable, &builder, &target, 31u)
                : match_atoms_epoch_view_builder_current(
                      variable, 31u, 0u, value, &builder, &target);
            CHECK(matched && builder.current.len == 1u &&
                      bindings_lookup_id(&builder.current, key) == value,
                  "either binding-key orientation preserves epoch identity");
            if (!structured)
                CHECK(arena_accounted_live_bytes(&target) == before,
                      "an identity-only binding key needs no temporary atom");
            /* Structured key presentation belongs to the surviving target,
             * even though the source variable is no longer available. */
            arena_free(&source);
            Atom *actual_key = matched && builder.current.len == 1u
                ? binding_variable_atom(
                      &target, bindings_entry_at(&builder.current, 0))
                : NULL;
            CHECK(actual_key && atom_eq(actual_key, expected_key),
                  "binding key presentation survives source arena release");
            bindings_builder_rollback(&builder, 0u);
            CHECK(builder.current.len == 0u && !bindings_has_loop(&builder.current),
                  "epoch-key rollback restores the empty store");
            bindings_builder_free(&builder);
            arena_free(&target); arena_free(&expected_arena);
        }
    }
    Arena source, target;
    arena_init(&source); arena_init(&target);
    Atom *variable = atom_var_with_id(&source, "escaping-epoch", UINT64_C(91002));
    VarId left_id = var_epoch_id(variable->var_id, 41u);
    VarId right_id = var_epoch_id(variable->var_id, 43u);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL), "escaping epoch builder");
    size_t before = arena_accounted_live_bytes(&target);
    CHECK(match_atoms_epoch_view_builder(
              variable, 41u, 0u, variable, &builder, &target, 41u) &&
              builder.current.len == 0u &&
              arena_accounted_live_bytes(&target) == before,
          "equal epoch identities unify without a binding or temporary atom");
    CHECK(match_atoms_epoch_view_builder(
              variable, 41u, 0u, variable, &builder, &target, 43u),
          "distinct epochs produce an alias instead of collapsing identity");
    arena_free(&source);
    Atom *alias = bindings_lookup_id(&builder.current, left_id);
    CHECK(alias && alias->kind == ATOM_VAR && alias->var_id == right_id,
          "a variable stored as a value survives source release");
    Atom *value = atom_int(&target, 7);
    CHECK(alias && bindings_builder_add_var_fresh(&builder, alias, value) &&
              bindings_resolve_atom_preview(&builder.current, alias) == value,
          "an escaping variable value remains usable by later binding");
    bindings_builder_rollback(&builder, 0u);
    CHECK(builder.current.len == 0u && !bindings_has_loop(&builder.current),
          "rollback removes both epoch alias and its later binding");
    bindings_builder_free(&builder); arena_free(&target);
}

int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols; g_hashcons=NULL;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    Arena arena; arena_init(&arena);
    test_runtime_stats_reset_counters();
    exercise(&arena,false); exercise(&arena,true); epoch_separation(&arena); plan_validation(&arena); test_nested_head_dereferences(&arena);
    test_type_wildcards(&arena);
    test_unary_spine(&arena);
    test_epoch_binding_keys();
    uint64_t elided=test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
    if (argc>1 && strcmp(argv[1],"--expect-elision")==0)
        CHECK(elided>0u,"candidate executes measured frame elision");
    fprintf(stderr,"elided_worklist_frames=%llu\n",(unsigned long long)elided);
    printf("MatchWorklistFrames checks=%u failures=%u\n",checks,failures);
    arena_free(&arena); bindings_thread_cache_free(); g_var_intern=NULL; var_intern_free(&vars);
    g_symbols=NULL; symbol_table_free(&symbols);
    return failures?1:0;
}
