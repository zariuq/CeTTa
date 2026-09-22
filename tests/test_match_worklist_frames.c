#include "atom.h"
#include "match.h"
#include "stats.h"
#include "tests/test_runtime_stats_stubs.h"
#include <stdio.h>
#include <string.h>

static bool lookup_frame_value(const Bindings *bindings, VarId id, BindingValue *value) {
    bool known = false;
    uint32_t row = 0u;
    if (!bindings_frame_index_test_lookup(bindings, id, &known, &row) || !known)
        return false;
    *value = bindings_lookup_value_id((Bindings *)bindings, id);
    return true;
}

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
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton==f,"first-child binding is retained");
    bindings_builder_rollback(&bb,0u);
    CHECK(RUN(atom_expr(a,NULL,0u),atom_expr(a,NULL,0u)),"empty expressions have no first child");

    /* A later mismatch must not be hoisted ahead of earlier bindings. */
    CHECK(!RUN(atom_expr3(a,f,x,u),atom_expr3(a,f,v,v)),"later literal mismatch rejects");
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton==v,"failure retains exactly the earlier binding prefix");
    bindings_builder_rollback(&bb,0u);
    CHECK(bb.current.len==0u,"caller rollback restores entry");
    CHECK(!RUN(atom_expr3(a,f,u,x),atom_expr3(a,f,v,u)),"early literal mismatch rejects");
    CHECK(bb.current.len==0u,"later variable is not bound after early mismatch");
    CHECK(!RUN(atom_expr2(a,atom_int(a,1),x),atom_expr2(a,atom_int(a,2),u)),"grounded first-child mismatch still rejects");
    CHECK(bb.current.len==0u,"grounded mismatch precedes later bindings");

    CHECK(RUN(atom_expr2(a,f,x),atom_expr2(a,f,y)),"equal-spelled distinct variables still unify");
    Atom *mapped=bindings_lookup_value_id(&bb.current, x->var_id).skeleton;
    CHECK(mapped && mapped->kind==ATOM_VAR && mapped->var_id==y->var_id,"variable aliases are not mistaken for equal symbols");
    bindings_builder_rollback(&bb,0u);
    CHECK(!RUN(atom_expr3(a,f,x,x),atom_expr3(a,f,u,v)),"repeated variable mismatch rejects");
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton==u,"repeated-variable failure keeps original orientation");
    bindings_builder_rollback(&bb,0u);
    CHECK(RUN(atom_expr3(a,f,x,x),atom_expr3(a,f,u,u)),"repeated variable agreement succeeds");
    CHECK(bb.current.len==1u,"repeated occurrence does not duplicate binding");
    bindings_builder_rollback(&bb,0u);
    /* The occurs check is applied at the bind: a bind that would close a
     * cycle is refused and the environment is left as it was. */
    CHECK(!RUN(x,atom_expr2(a,f,x)),"a direct cycle is refused at the bind");
    CHECK(bb.current.len==0u && !bindings_has_loop(&bb.current),"a refused bind leaves the environment untouched");
    CHECK(RUN(x,atom_expr2(a,f,y)),"binding to an expression over another variable is admitted");
    CHECK(!RUN(y,atom_expr2(a,f,x)),"an indirect cycle is refused at the second bind");
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton && !bindings_lookup_value_id(&bb.current, y->var_id).skeleton && !bindings_has_loop(&bb.current),"the refused second bind keeps the first and stays acyclic");
    CHECK(RUN(y,atom_expr2(a,f,u)),"a bind that closes no cycle is admitted after a refusal");
    bindings_builder_rollback(&bb,0u);
    CHECK(bb.current.len==0u && !bindings_has_loop(&bb.current),"rollback restores the empty acyclic environment");

    Atom *shared=atom_expr2(a,f,u);
    CHECK(RUN(atom_expr3(a,f,shared,shared),atom_expr3(a,f,shared,shared)),"shared finite DAG may re-enter through sibling paths");
    Atom *cycle=atom_expr2(a,f,u);
    cycle->expr.elems[1]=cycle;
    CHECK(!RUN(cycle,cycle),"cyclic raw expression still fails active-path audit");
    cycle->expr.elems[1]=u;
    cycle->expr.elems[0]=cycle;
    CHECK(!RUN(cycle,cycle),"first-child raw cycle still fails active-path audit");
    cycle->expr.elems[0]=f;
    {
        Atom *spine=u;
        unsigned k;
        for (k=0;k<64u;k++)
            spine=atom_expr2(a,f,spine);
        CHECK(RUN(spine,spine),"finite eval-heap spine is reflexive at one occurrence");
        CHECK(bb.current.len==0u,"identity on a finite spine writes no bindings");
        bindings_builder_rollback(&bb,0u);
        Atom *copy=u;
        for (k=0;k<64u;k++)
            copy=atom_expr2(a,f,copy);
        CHECK(RUN(spine,copy),"equal finite eval-heap spines still unify");
        bindings_builder_rollback(&bb,0u);
    }
    bindings_builder_rollback(&bb,0u);
    {
        Atom *open_term = atom_expr2(a,f,x);
        uint64_t open_before = test_runtime_stats_counter(
            CETTA_RUNTIME_COUNTER_MATCH_SHARED_OPEN_REFLEXIVITY_COMMIT);
        CHECK(RUN(open_term,open_term),
              "finite open occurrence is reflexive under one environment");
        CHECK(bb.current.len==0u,
              "open identity writes no bindings");
        if (!epoch) {
            CHECK(test_runtime_stats_counter(
                      CETTA_RUNTIME_COUNTER_MATCH_SHARED_OPEN_REFLEXIVITY_COMMIT) >
                      open_before,
                  "decoded open identity is the SubstitutionAlgebra identity");
        }
        bindings_builder_rollback(&bb,0u);
    }

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
        all_bound=all_bound && bindings_lookup_value_id(&bb.current, ls[i]->var_id).skeleton==v;
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
    BindingValue lm=bindings_lookup_value_id(&bb.current, l), rm=bindings_lookup_value_id(&bb.current, r);
    CHECK(bindings_has_bound_values(&bb.current) &&
          ((lm.skeleton && binding_value_variable_id(lm)==r) ||
           (rm.skeleton && binding_value_variable_id(rm)==l)),
          "equal source pointers do not erase distinct activation identities");
    bindings_builder_rollback(&bb,0u);
    term=atom_expr2(a,x,f);
    CHECK(match_atoms_epoch_view_builder(term,21u,0u,term,&bb,a,22u),"variable heads retain source activations");
    lm=bindings_lookup_value_id(&bb.current, l); rm=bindings_lookup_value_id(&bb.current, r);
    CHECK(bindings_has_bound_values(&bb.current) &&
          ((lm.skeleton && binding_value_variable_id(lm)==r) ||
           (rm.skeleton && binding_value_variable_id(rm)==l)),
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

static void plan_head_order(Arena *a) {
    Atom *f = atom_symbol(a, "ord-f");
    Atom *u = atom_symbol(a, "ord-u");
    Atom *w = atom_symbol(a, "ord-w");
    Atom *x = atom_var_with_id(a, "ord-x", UINT64_C(51001));
    Atom *pattern = atom_expr3(a, f, x, u);
    Atom *goal = atom_expr3(a, f, w, w);
    CettaOpenPatternPlan children[3] = {
        {.source = f, .kind = ATOM_SYMBOL},
        {.source = x, .kind = ATOM_VAR, .variable_mask = UINT64_C(1)},
        {.source = u, .kind = ATOM_SYMBOL},
    };
    CettaOpenPatternPlan root = {
        .source = pattern, .kind = ATOM_EXPR,
        .children = children, .child_count = 3u,
        .variable_mask = UINT64_C(1),
    };
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb, NULL), "head-order builder");
    CHECK(!match_atoms_epoch_builder_rule_local_planned(
              goal, pattern, &root, &bb, a, 7u),
          "planned constructor mismatch still rejects");
    CHECK(bb.current.len == 0u,
          "discriminating constructor is checked before binding extension");
    bindings_builder_free(&bb);
    CHECK(bindings_builder_init(&bb, NULL), "unplanned prefix builder");
    CHECK(!match_atoms_builder(pattern, goal, &bb),
          "unplanned later mismatch still rejects");
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton == w,
          "unplanned matching retains the left-to-right failure prefix");
    bindings_builder_free(&bb);
}

static void planned_cycle_support_owns_context(Arena *a) {
    Atom *head = atom_symbol(a, "cycle-plan-head");
    Atom *destination = atom_var_with_id(
        a, "cycle-plan-destination", UINT64_C(51011));
    Atom *source = atom_var_with_id(
        a, "cycle-plan-source", UINT64_C(51012));
    Atom *pattern = atom_expr2(a, head, source);
    VarId variable_ids[] = {source->var_id};
    CettaOpenPatternPlan children[2] = {
        {.source = head, .kind = ATOM_SYMBOL},
        {.source = source, .kind = ATOM_VAR,
         .variable_mask = UINT64_C(1)},
    };
    CettaOpenPatternPlan plan = {
        .source = pattern,
        .kind = ATOM_EXPR,
        .children = children,
        .child_count = 2u,
        .variable_ids = variable_ids,
        .variable_mask = UINT64_C(1),
    };
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL),
          "planned cycle-support builder");
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_epoch_builder_rule_local_planned(
              destination, pattern, &plan, &builder, a, 17u),
          "planned open expression binds through its contextual inventory");
    BindingValue stored = bindings_lookup_value_id(
        &builder.current, destination->var_id);
    CHECK(stored.skeleton == pattern && binding_value_is_contextual(stored) &&
              stored.epoch == 17u,
          "planned open expression remains syntax plus context");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_UNOWNED_CONTEXTUAL) == 0u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE) > 0u,
          "planned cycle support reads its owned frame coordinate");
    bindings_builder_free(&builder);

    Atom *closed = atom_expr2(a, head, atom_int(a, 5));
    CettaOpenPatternPlan closed_children[2] = {
        {.source = head, .kind = ATOM_SYMBOL},
        {.source = closed->expr.elems[1], .kind = ATOM_GROUNDED},
    };
    CettaOpenPatternPlan closed_plan = {
        .source = closed,
        .kind = ATOM_EXPR,
        .children = closed_children,
        .child_count = 2u,
    };
    CHECK(bindings_builder_init(&builder, NULL),
          "closed cycle-support control builder");
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_epoch_builder_rule_local_planned(
              destination, closed, &closed_plan, &builder, a, 18u) &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 0u,
          "closed planned values do not manufacture an empty context frame");
    bindings_builder_free(&builder);
}

static void unplanned_activation_owns_complete_context(Arena *a) {
    Atom *head = atom_symbol(a, "unplanned-frame-head");
    Atom *left_value = atom_symbol(a, "unplanned-frame-value");
    Atom *source = atom_var_with_id(
        a, "unplanned-frame-source", UINT64_C(0x123400000000c751));
    Atom *pattern = atom_expr3(a, head, source, source);
    Atom *goal = atom_expr3(a, head, left_value, left_value);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL),
          "unplanned activation builder");
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_epoch_builder(
              goal, pattern, &builder, a, 23u),
          "unplanned stored pattern matches in one contextual activation");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_UNOWNED_CONTEXTUAL) == 0u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE) >= 2u,
          "unplanned repeated source reads only its owned frame coordinate");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_NEW) == 1u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_EXTENDED) == 0u,
          "unplanned activation admits the complete normalized source inventory once");
    bindings_builder_free(&builder);

    Atom *closed = atom_expr2(a, head, left_value);
    CHECK(bindings_builder_init(&builder, NULL),
          "unplanned closed-pattern builder");
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_epoch_builder(
              closed, closed, &builder, a, 24u) &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 0u,
          "unplanned closed patterns allocate no contextual frame");
    bindings_builder_free(&builder);
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
    CHECK(bindings_lookup_value_id(&bindings, x->var_id).skeleton == f,
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
        CHECK(matched && bindings_lookup_value_id(&builder.current, x->var_id).skeleton == u,
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
            CHECK(matched && bindings_has_bound_values(&builder.current) &&
                      bindings_lookup_value_id(&builder.current, key).skeleton == value,
                  "either binding-key orientation preserves epoch identity");
            if (!structured)
                CHECK(arena_accounted_live_bytes(&target) == before,
                      "an identity-only binding key needs no temporary atom");
            /* Structured key presentation belongs to the surviving target,
             * even though the source variable is no longer available. */
            arena_free(&source);
            Atom *serialized = matched
                ? bindings_to_atom(&target, &builder.current) : NULL;
            Atom *assignments = serialized && serialized->kind == ATOM_EXPR &&
                    serialized->expr.len == 3u
                ? serialized->expr.elems[1] : NULL;
            Atom *actual_key = assignments &&
                    assignments->kind == ATOM_EXPR &&
                    assignments->expr.len == 1u &&
                    assignments->expr.elems[0]->kind == ATOM_EXPR &&
                    assignments->expr.elems[0]->expr.len == 2u
                ? assignments->expr.elems[0]->expr.elems[0] : NULL;
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
    BindingValue stored_alias = bindings_lookup_value_id(&builder.current, left_id);
    CHECK(stored_alias.skeleton && binding_value_variable_id(stored_alias) == right_id,
          "a variable stored as a value survives source release");
    Atom *alias = binding_value_materialize(&target, stored_alias);
    Atom *value = atom_int(&target, 7);
    BindingValue preview;
    CHECK(alias && bindings_builder_add_var_fresh(&builder, alias, value) &&
              bindings_resolve_value_preview(&builder.current, binding_value_from_atom(alias), &preview) &&
              preview.skeleton == value,
          "an escaping variable value remains usable by later binding");
    bindings_builder_rollback(&builder, 0u);
    CHECK(builder.current.len == 0u && !bindings_has_loop(&builder.current),
          "rollback removes both epoch alias and its later binding");
    bindings_builder_free(&builder); arena_free(&target);
}

static void binding_value_context(Arena *a) {
    Arena source;
    arena_init(&source);
    Atom *x = atom_var_with_id(&source, "value-x", UINT64_C(46001));
    Atom *skeleton = atom_expr2(&source, atom_symbol(&source, "value-box"), x);
    BindingValue value = binding_value_from_context(skeleton, 191u);
    BindingValue retained = value;
    retained.skeleton = atom_deep_copy(a, value.skeleton);
    Atom *materialized = binding_value_materialize(a, value);
    CHECK(materialized && materialized->expr.elems[1]->var_id ==
              var_epoch_id(x->var_id, 191u),
          "contextual materialization qualifies variables at the Atom boundary");
    CHECK(binding_value_equal(value, binding_value_from_atom(materialized)),
          "contextual syntax equals its explicitly materialized interpretation");
    CHECK(!binding_value_equal(value, binding_value_from_context(skeleton, 192u)),
          "the same open skeleton in distinct contexts is not the same value");
    CHECK(binding_value_equal(
              binding_value_from_context(atom_int(a, 7), 191u),
              binding_value_from_context(atom_int(a, 7), 192u)),
          "a ground value is independent of lexical context");
    Atom *qualified = materialized->expr.elems[1];
    BindingValue normalized_qualified =
        binding_value_from_context_kind(
            qualified, 0u, BINDING_VALUE_MATERIALIZED);
    BindingValue ordinary_unqualified =
        binding_value_from_context_kind(
            x, 0u, BINDING_VALUE_MATERIALIZED);
    CHECK(binding_value_is_contextual(normalized_qualified) &&
              normalized_qualified.epoch == 191u &&
              binding_value_variable_id(normalized_qualified) ==
                  qualified->var_id,
          "a qualified scalar variable retains its explicit frame coordinate");
    CHECK(!binding_value_is_contextual(ordinary_unqualified) &&
              binding_value_variable_id(ordinary_unqualified) == x->var_id,
          "an ordinary scalar variable does not acquire a frame");
    CHECK(binding_value_equal(binding_value_from_context(x, 192u),
                              binding_value_from_context(qualified, 192u)),
          "contextual equality compares requalified identities, not old syntax epochs");
    CHECK(!binding_value_equal(binding_value_from_atom(qualified),
                              binding_value_from_context(qualified, 0u)),
          "context zero and an already qualified materialized variable remain distinct");
    Bindings summary;
    bindings_init(&summary);
    bool summary_known = false;
    uint32_t summary_entry = 0u;
    CHECK(bindings_add_id(
              &summary, UINT64_C(46009), SYMBOL_ID_NONE, materialized) &&
              bindings_frame_index_test_lookup(
                  &summary, qualified->var_id,
                  &summary_known, &summary_entry) && summary_known,
          "materialized epoch syntax registers the frame it names");
    CHECK(bindings_prepare_logical_write(&summary),
          "materialized summary fixture is exclusively writable");
    summary.entries[0].value = value;
    bindings_invalidate_after_key_rewrite(&summary);
    summary_known = false;
    CHECK(bindings_frame_index_test_lookup(
              &summary, qualified->var_id,
              &summary_known, &summary_entry) && summary_known,
          "a contextual value rewrite keeps its named frame registered");
    summary.entries[0].value = binding_value_from_atom(materialized);
    bindings_invalidate_after_key_rewrite(&summary);
    summary_known = false;
    CHECK(bindings_frame_index_test_lookup(
              &summary, qualified->var_id,
              &summary_known, &summary_entry) && summary_known,
          "a materialized value rewrite keeps its named frame registered");
    bindings_free(&summary);
    materialized = atom_deep_copy(a, materialized);
    qualified = materialized->expr.elems[1];
    arena_free(&source);
    CHECK(binding_value_equal(retained, binding_value_from_atom(materialized)),
          "transporting syntax retains lexical metadata without the source arena");

    BindingsBuilder builder;
    Bindings sibling;
    CHECK(bindings_builder_init(&builder, NULL), "value observation builder");
    CHECK(bindings_clone(&sibling, &builder.current), "capture unrefined sibling");
    uint32_t mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_var_fresh(&builder, qualified, atom_int(a, 7)),
          "refine a variable after its contextual value was created");
    Atom *observed = bindings_apply(&builder.current, a,
                                   binding_value_materialize(a, retained));
    CHECK(atom_eq(observed, atom_expr2(a, atom_symbol(a, "value-box"), atom_int(a, 7))),
          "later branch refinements remain visible through a retained context");
    CHECK(atom_eq(bindings_apply(&sibling, a, binding_value_materialize(a, retained)),
                  materialized),
          "a sibling keeps its own substitution version for the same lexical value");
    bindings_builder_rollback(&builder, mark);
    CHECK(atom_eq(bindings_apply(&builder.current, a,
                                binding_value_materialize(a, retained)), materialized),
          "rollback restores an unbound value without rewriting its lexical context");
    bindings_free(&sibling);
    bindings_builder_free(&builder);
}

static void materialized_frame_branch_lifetime(Arena *a) {
    Atom *source = atom_var_with_id(
        a, "lease-source", UINT64_C(46101));
    Atom *holder = atom_var_with_id(
        a, "lease-holder", UINT64_C(46102));
    Atom *qualified = atom_var_with_id(
        a, "lease-source", var_epoch_id(source->var_id, 193u));
    Atom *box = atom_expr2(
        a, atom_symbol(a, "lease-box"), qualified);
    Atom *seven = atom_int(a, 7);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL),
          "materialized frame lifetime builder");
    test_runtime_stats_reset_counters();
    CHECK(bindings_builder_add_var_fresh(&builder, holder, box),
          "a stored manufactured value registers every frame it names");
    uint32_t retained_mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_var_fresh(&builder, qualified, seven) &&
              atom_eq(bindings_apply(&builder.current, a, box),
                      atom_expr2(a, atom_symbol(a, "lease-box"), seven)),
          "a leased materialized coordinate resolves through its frame");
    bindings_builder_rollback(&builder, retained_mark);
    bool qualified_known = false;
    uint32_t qualified_entry = 0u;
    CHECK(atom_eq(bindings_apply(&builder.current, a, box), box) &&
              bindings_frame_index_test_lookup(
                  &builder.current, qualified->var_id,
                  &qualified_known, &qualified_entry) &&
              qualified_known && qualified_entry == UINT32_MAX,
          "rollback keeps a manufactured frame for the branch value that owns it");
    bindings_builder_rollback(&builder, 0u);
    qualified_known = true;
    CHECK(builder.current.len == 0u &&
              bindings_frame_index_test_lookup(
                  &builder.current, qualified->var_id,
                  &qualified_known, &qualified_entry) &&
              !qualified_known,
          "rolling back the branch value removes the manufactured frame at its trail mark");
    CHECK(builder.frame_registration_undo_len == 0u,
          "trail-directed frame lifetime consumes its inverse records exactly");
    bindings_builder_free(&builder);
}

/* Author contextual rows directly while their production writers are being
 * migrated.  Compare dependency projection with independently materialized
 * rows and exercise both the incremental and whole-graph cycle authorities. */
static void contextual_binding_dependencies(Arena *a) {
    Atom *x = atom_var_with_id(a, "dependency-x", UINT64_C(47001));
    Atom *y = atom_var_with_id(a, "dependency-y", UINT64_C(47002));
    Atom *z = atom_var_with_id(a, "dependency-z", UINT64_C(47003));
    Atom *q = atom_var_with_id(a, "dependency-q", UINT64_C(47004));
    Atom *xa = atom_var_with_id(a, "dependency-x", var_epoch_id(x->var_id, 200u));
    Atom *yb = atom_var_with_id(a, "dependency-y", var_epoch_id(y->var_id, 201u));
    Atom *zb = atom_var_with_id(a, "dependency-z", var_epoch_id(z->var_id, 201u));
    Atom *qc = atom_var_with_id(a, "dependency-q", var_epoch_id(q->var_id, 202u));
    Atom *wrong_y = atom_var_with_id(a, "dependency-y", var_epoch_id(y->var_id, 203u));
    Atom *box = atom_symbol(a, "dependency-box");
    for (unsigned large = 0u; large < 2u; large++) {
        for (unsigned multi = 0u; multi < 2u; multi++) {
            Bindings source;
            bindings_init(&source);
            CHECK(bindings_add_var(&source, xa, atom_int(a, 1)) &&
                  bindings_add_var(&source, yb, atom_int(a, 2)) &&
                  bindings_add_var(&source, zb, atom_int(a, 3)) &&
                  bindings_add_var(&source, wrong_y, atom_int(a, 99)),
                  "construct contextual dependency fixture storage");
            bool padding_ready = true;
            for (unsigned i = 0u; i < (large ? 80u : 0u); i++)
                padding_ready = padding_ready && bindings_add_id(
                    &source, UINT64_C(48000) + i, SYMBOL_ID_NONE, atom_int(a, i));
            CHECK(padding_ready, "unrelated bindings exercise indexed dependency traversal");
            CHECK(bindings_prepare_logical_write(&source), "own dependency rows before changing representation");
            CHECK(bindings_rewrite_value_id(&source, xa->var_id,
                      binding_value_from_context(
                          multi ? atom_expr3(a, box, y, z) : atom_expr2(a, box, y), 201u)) &&
                  bindings_rewrite_value_id(&source, yb->var_id,
                      binding_value_from_context(atom_expr2(a, box, q), 202u)),
                  "store contextual dependencies through authoritative slots");
            bindings_invalidate_after_key_rewrite(&source);
            CHECK(!bindings_has_loop(&source), "contextual chain ending in an unbound variable is acyclic");
            Atom *serialized = bindings_to_atom(a, &source);
            Bindings reference;
            CHECK(serialized && bindings_from_atom(serialized, &reference),
                  "materialized oracle for contextual dependency graph");
            Bindings projected, reference_projected;
            CHECK(bindings_project_reachable(&source, &xa, 1u, &projected) &&
                  bindings_project_reachable(&reference, &xa, 1u, &reference_projected),
                  "project the contextual and materialized dependency graphs");
            bool projection_equal =
                bindings_eq(&projected, &reference_projected);
            bool projection_excludes_wrong =
                !bindings_lookup_value_id(
                    &projected, wrong_y->var_id).skeleton;
            CHECK(projection_equal && projection_excludes_wrong,
                  "projection keeps qualified dependencies and excludes same-spelling other frames");
            /* The independent whole-graph oracle established this summary. */
            source.cycle_state = 1u;
            BindingsBuilder builder;
            CHECK(bindings_builder_init(&builder, &source), "branch from the contextual dependency graph");
            uint32_t mark = bindings_builder_save(&builder);
            test_runtime_stats_reset_counters();
            bool cycle_added = bindings_builder_add_var_fresh(
                &builder, qc, atom_expr2(a, box, xa));
            bool still_acyclic = !bindings_has_loop(&builder.current);
            if (cycle_added || !still_acyclic)
                fprintf(stderr, "contextual-cycle variant large=%u multi=%u added=%u acyclic=%u state=%u\n",
                    large, multi, cycle_added, still_acyclic, builder.current.cycle_state);
            CHECK(!cycle_added && still_acyclic && bindings_eq(&builder.current, &source),
                  "a cycle through contextual values across frames is refused at the bind");
            CHECK(!large || test_runtime_stats_counter(
                      CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_STEP) > 0u,
                  "larger contextual graphs exercise the indexed support path");
            CHECK(!multi || test_runtime_stats_counter(
                      CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_GENERAL_ITEM) > 0u,
                  "multi-variable contextual graphs exercise full support traversal");
            bindings_builder_rollback(&builder, mark);
            CHECK(!bindings_has_loop(&builder.current) && bindings_eq(&builder.current, &source),
                  "rollback restores both the contextual graph and its acyclic summary");
            bindings_builder_free(&builder);
            bindings_free(&projected);
            bindings_free(&reference_projected);
            bindings_free(&reference);
            CHECK(bindings_prepare_logical_write(&source), "detach source after branch checks");
            CHECK(bindings_rewrite_value_id(&source, yb->var_id,
                      binding_value_from_context(atom_expr2(a, box, x), 199u)),
                  "rewrite dependency in its own lexical frame");
            bindings_invalidate_after_key_rewrite(&source);
            CHECK(!bindings_has_loop(&source), "same source variable in a different frame does not close a cycle");
            CHECK(bindings_rewrite_value_id(&source, yb->var_id,
                      binding_value_from_context(atom_expr2(a, box, x), 200u)),
                  "rewrite dependency in its own lexical frame");
            bindings_invalidate_after_key_rewrite(&source);
            CHECK(bindings_has_loop(&source), "whole-graph oracle detects the cycle in the actual lexical frame");
            bindings_free(&source);
        }
    }
}

static void dense_contextual_values(Arena *a) {
    Atom *q = atom_var_with_id(a, "dense-q", UINT64_C(51001));
    Atom *w = atom_var_with_id(a, "dense-w", UINT64_C(51002));
    Atom *u = atom_var_with_id(a, "dense-u", UINT64_C(51003));
    Atom *z = atom_var_with_id(a, "dense-z", UINT64_C(51004));
    Atom *qa = atom_var_with_id(a, "dense-q", var_epoch_id(q->var_id, 400u));
    Atom *wa = atom_var_with_id(a, "dense-w", var_epoch_id(w->var_id, 400u));
    Atom *zb = atom_var_with_id(a, "dense-z", var_epoch_id(z->var_id, 401u));
    Atom *wb = atom_var_with_id(a, "dense-w", var_epoch_id(w->var_id, 401u));
    Atom *inner = atom_symbol(a, "dense-inner");
    Atom *outer = atom_symbol(a, "dense-outer");
    Atom *nested = atom_expr3(a, inner, z, w);
    Atom *source = atom_expr3(a, outer, q, w);
    Atom *expected_inner = atom_expr3(a, inner, atom_int(a, 7), atom_int(a, 8));
    Atom *expected = atom_expr3(a, outer, expected_inner, atom_int(a, 11));
    BindingsBuilder builder;
    VarId frame400_ids[] = {q->var_id, w->var_id, u->var_id};
    VarId frame401_ids[] = {w->var_id, z->var_id};
    CHECK(bindings_builder_init(&builder, NULL) &&
          bindings_builder_register_contextual_frame(
              &builder, frame400_ids, 3u, 400u) &&
          bindings_builder_register_contextual_frame(
              &builder, frame401_ids, 2u, 401u) &&
          bindings_builder_add_var_fresh(&builder, qa, atom_int(a, 0)) &&
          bindings_builder_add_var_fresh(&builder, wa, atom_int(a, 11)) &&
          bindings_builder_add_var_fresh(&builder, zb, atom_int(a, 7)) &&
          bindings_builder_add_var_fresh(&builder, wb, atom_int(a, 8)),
          "construct two lexical contexts for dense value observation");
    CHECK(bindings_rewrite_value_id(
              &builder.current, qa->var_id,
              binding_value_from_context(nested, 401u)),
          "replace a dense slot with its contextual closure");
    VarId ids[] = {q->var_id, w->var_id, u->var_id};
    Atom *variables[] = {q, w, u};
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    CHECK(bindings_activation_view_prepare(&frame, &builder, ids, variables, 3u, 400u, 0u),
          "dense preparation retains stored lexical metadata");

    Atom *persistent_target = atom_var_with_id(
        a, "dense-persistent-target", UINT64_C(51005));
    uint32_t persistent_mark = bindings_builder_save(&builder);
    frame.source_kind = BINDING_VALUE_CONTEXTUAL_PERSISTENT;
    CHECK(match_atoms_activation_view_builder_rule_local(
              source, &frame, persistent_target, &builder, a, 402u),
          "a persistent source frame binds without changing its ownership kind");
    BindingValue persistent_value = bindings_lookup_value_id(
        &builder.current,
        var_epoch_id(persistent_target->var_id, 402u));
    CHECK(persistent_value.skeleton == source &&
              persistent_value.kind == BINDING_VALUE_CONTEXTUAL_PERSISTENT &&
              persistent_value.epoch == 400u,
          "a retained persistent context keeps its source pointer and lexical identity");
    bindings_builder_rollback(&builder, persistent_mark);
    frame.source_kind = BINDING_VALUE_CONTEXTUAL;
    CHECK(bindings_activation_view_available(&frame, &builder.current),
          "rollback restores the ordinary contextual frame view");
    BindingValue value;
    CHECK(lookup_frame_value(&builder.current, qa->var_id, &value) &&
          value.kind == BINDING_VALUE_CONTEXTUAL && value.epoch == 401u && value.skeleton == nested,
          "dense slot lookup returns the contextual value without allocating a renamed term");
    BindingsTermCursorContextV1 cursor_context = {.bindings = &builder.current, .frame = &frame};
    CettaGsltTermCursorV1 cursor_result;
    CHECK(bindings_resolve_term_cursor_v1(&cursor_context,
              (CettaGsltTermCursorV1){q, &frame}, &cursor_result) == CETTA_GSLT_TERM_VIEW_DEFER_V1 &&
          bindings_resolve_term_cursor_v1(&cursor_context,
              (CettaGsltTermCursorV1){qa, NULL}, &cursor_result) == CETTA_GSLT_TERM_VIEW_DEFER_V1,
          "Atom-only cursor observation declines a contextual expression instead of exposing a wildcard");
    CHECK(atom_eq(bindings_apply_activation_view_then_all(&builder.current, a, source, &frame), expected),
          "dense observation applies the stored context and restores the sibling context");
    CHECK(atom_eq(bindings_apply_activation_view_slot_then_all(&builder.current, a, &frame, q, 0u), expected_inner),
          "direct slot observation follows stored-context dependencies");
    Atom *root = bindings_resolve_activation_view_slot_root(&builder.current, a, &frame, q, 0u);
    CHECK(root && root->kind == ATOM_EXPR &&
          root->expr.elems[1]->var_id == zb->var_id && root->expr.elems[2]->var_id == wb->var_id,
          "root-only observation reifies lexical identities without forcing expression children");
    CHECK(match_atoms_activation_view_builder_current(source, &frame, expected, &builder, a),
          "generic epoch worklist follows contextual dense values and restores siblings");
    CHECK(match_atoms_epoch_view_builder_current(source, 400u, 0u, expected, &builder, a),
          "suffix lookup preserves context when no dense view is supplied");
    CHECK(match_atoms_epoch_view_builder_current(expected_inner, 0u, 0u, qa, &builder, a),
          "a contextual value reached from the right operand keeps its own lexical frame");
    Atom *wrong = atom_expr3(a, outer, expected_inner, atom_int(a, 12));
    CHECK(!match_atoms_activation_view_builder_current(source, &frame, wrong, &builder, a),
          "generic contextual matching rejects a wrong sibling value");
    CettaOpenPatternInstruction program[] = {
        {.source = expected, .subtree_span = 7u},
        {.source = outer, .subtree_span = 1u},
        {.source = expected_inner, .subtree_span = 4u},
        {.source = inner, .subtree_span = 1u},
        {.source = expected_inner->expr.elems[1], .subtree_span = 1u},
        {.source = expected_inner->expr.elems[2], .subtree_span = 1u},
        {.source = expected->expr.elems[2], .subtree_span = 1u},
    };
    CettaOpenPatternPlan plan = {.source = expected, .kind = ATOM_EXPR,
        .child_count = 3u, .linear_program = program, .linear_program_len = 7u};
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_activation_view_builder_rule_local_linear(
              source, &frame, expected, &plan, &builder, a, 402u) &&
          test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_NODE_VISIT) > 0u,
          "compiled pattern walk queues the context of each sibling independently");
    uint32_t mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_id_fresh(&builder, var_epoch_id(u->var_id, 400u), u->sym_id, atom_int(a, 9)),
          "append a live dense slot before rollback");
    bindings_builder_rollback(&builder, mark);
    CHECK(lookup_frame_value(&builder.current, qa->var_id, &value) &&
          value.kind == BINDING_VALUE_CONTEXTUAL && value.epoch == 401u,
          "rollback preserves the lexical metadata of retained dense slots");
    bindings_activation_view_free(&frame);
    bindings_builder_free(&builder);
}

static void contextual_observation_visibility(Arena *a) {
    Atom *q = atom_var_with_id(a, "visible-q", UINT64_C(52001));
    Atom *p = atom_var_with_id(a, "visible-p", UINT64_C(52002));
    Atom *qa = atom_var_with_id(a, "visible-q", var_epoch_id(q->var_id, 410u));
    Atom *pa = atom_var_with_id(a, "visible-p", var_epoch_id(p->var_id, 410u));
    Atom *head = atom_symbol(a, "visible-pair");
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL) &&
          bindings_builder_add_var_fresh(&builder, qa, atom_int(a, 7)) &&
          bindings_builder_add_id_fresh(&builder, UINT64_C(52003), SYMBOL_ID_NONE, atom_int(a, 0)),
          "construct separate observation visibility domains");
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    VarId ids[] = {q->var_id, p->var_id};
    Atom *variables[] = {q, p};
    CHECK(bindings_activation_view_prepare(&frame, &builder, ids, variables, 2u, 410u, 1u) &&
          match_binding_values_builder(binding_value_from_atom(pa),
              binding_value_from_context(q, 410u), &builder),
          "a new slot alias follows a binding preceding its activation boundary");
    Atom *source = atom_expr3(a, head, q, p);
    Atom *observed = bindings_apply_activation_view_then_all(&builder.current, a, source, &frame);
    CHECK(observed && atom_eq(observed, atom_expr3(a, head, qa, atom_int(a, 7))),
          "suffix-view memo cannot hide a prefix binding from a stored lexical value");
    Atom *ground = NULL;
    CHECK(bindings_resolve_epoch_view_ground(&builder.current, p, 410u, 1u, &ground) &&
          ground && atom_eq(ground, atom_int(a, 7)), "ground admission follows a contextual variable alias");
    bindings_activation_view_free(&frame);
    bindings_builder_free(&builder);
}

typedef struct {
    VarId expected;
    unsigned calls;
    bool correct_identity;
    bool refuse;
} ContextualRewriteProbe;

static Atom *contextual_rewrite_probe(Arena *a, Atom *var, void *context) {
    ContextualRewriteProbe *probe = context;
    probe->calls++;
    probe->correct_identity = probe->correct_identity &&
        var->kind == ATOM_VAR && var->var_id == probe->expected;
    return probe->refuse ? NULL : atom_int(a, 42);
}

static void contextual_ordinary_substitution(Arena *a) {
    Atom *p = atom_var_with_id(a, "ordinary-p", UINT64_C(53001));
    Atom *x = atom_var_with_id(a, "ordinary-x", UINT64_C(53002));
    Atom *y = atom_var_with_id(a, "ordinary-y", UINT64_C(53003));
    Atom *pa = atom_var_with_id(a, "ordinary-p", var_epoch_id(p->var_id, 430u));
    Atom *xa = atom_var_with_id(a, "ordinary-x", var_epoch_id(x->var_id, 430u));
    Atom *xb = atom_var_with_id(a, "ordinary-x", var_epoch_id(x->var_id, 431u));
    Atom *yc = atom_var_with_id(a, "ordinary-y", var_epoch_id(y->var_id, 432u));
    Atom *head = atom_symbol(a, "ordinary-pair");
    Bindings bindings, reference;
    bindings_init(&bindings);
    bindings_init(&reference);
    VarId frame430_ids[] = {p->var_id, x->var_id};
    VarId frame431_ids[] = {x->var_id};
    VarId frame432_ids[] = {y->var_id};
    CHECK(bindings_register_contextual_frame(
              &bindings, frame430_ids, 2u, 430u) &&
          bindings_register_contextual_frame(
              &bindings, frame431_ids, 1u, 431u) &&
          bindings_register_contextual_frame(
              &bindings, frame432_ids, 1u, 432u) &&
          bindings_add_var(&bindings, pa, atom_int(a, 0)) &&
          bindings_add_var(&bindings, xb, atom_int(a, 0)) &&
          bindings_add_var(&bindings, xa, atom_int(a, 99)) &&
          bindings_rewrite_value_id(
              &bindings, pa->var_id,
              binding_value_from_context(atom_expr3(a, head, x, x), 431u)) &&
          bindings_rewrite_value_id(
              &bindings, xb->var_id,
              binding_value_from_context(y, 432u)),
          "construct ordinary contextual substitution");
    CHECK(bindings_register_contextual_frame(
              &reference, frame430_ids, 2u, 430u) &&
          bindings_register_contextual_frame(
              &reference, frame431_ids, 1u, 431u) &&
          bindings_register_contextual_frame(
              &reference, frame432_ids, 1u, 432u) &&
          bindings_add_var(&reference, pa, atom_expr3(a, head, xb, xb)) &&
          bindings_add_var(&reference, xb, yc) &&
          bindings_add_var(&reference, xa, atom_int(a, 99)),
          "construct independently qualified ordinary substitution oracle");
    Atom *unbound = atom_expr3(a, head, yc, yc);
    CHECK(atom_eq(bindings_apply(&bindings, a, pa), unbound) &&
          atom_eq(bindings_apply(&bindings, a, pa), bindings_apply(&reference, a, pa)),
          "ordinary observation follows stored aliases and excludes another frame's binding");
    ContextualRewriteProbe probe = {.expected = yc->var_id, .correct_identity = true};
    CHECK(atom_eq(bindings_apply_rewrite_vars(&bindings, a, pa, contextual_rewrite_probe, &probe),
                  atom_expr3(a, head, atom_int(a, 42), atom_int(a, 42))) &&
          probe.calls == 1u && probe.correct_identity,
          "rewrite callback receives the effective identity once for a shared variable");
    probe.calls = 0u;
    probe.refuse = true;
    CHECK(!bindings_apply_rewrite_vars(&bindings, a, pa, contextual_rewrite_probe, &probe) &&
          probe.calls == 1u && probe.correct_identity,
          "contextual observation propagates a rewrite callback's refusal");
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, &bindings), "create contextual observation branch");
    uint32_t mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_var_fresh(&builder, yc, atom_int(a, 7)) &&
          atom_eq(bindings_apply(&builder.current, a, pa),
                  atom_expr3(a, head, atom_int(a, 7), atom_int(a, 7))) &&
          atom_eq(bindings_apply(&bindings, a, pa), unbound),
          "ordinary observation sees its branch's refinement without changing a sibling");
    bindings_builder_rollback(&builder, mark);
    CHECK(atom_eq(bindings_apply(&builder.current, a, pa), unbound),
          "rollback restores unresolved contextual values");
    bindings_builder_free(&builder);
    CHECK(!bindings_add_var(&bindings, yc, pa) && !bindings_has_loop(&bindings) &&
          !bindings_add_var(&reference, yc, pa) && !bindings_has_loop(&reference),
          "a contextual alias cycle is refused at the bind in both representations");
    CHECK(atom_eq(bindings_apply(&bindings, a, pa), bindings_apply(&reference, a, pa)),
          "observation after a refused bind agrees with the materialized oracle");
    probe = (ContextualRewriteProbe){.expected = yc->var_id, .correct_identity = true};
    CHECK(atom_eq(bindings_apply_rewrite_vars(&bindings, a, pa, contextual_rewrite_probe, &probe),
                  atom_expr3(a, head, atom_int(a, 42), atom_int(a, 42))) &&
          probe.calls == 1u && probe.correct_identity,
          "rewrite callback still sees the effective identity after a refused bind");
    bindings_free(&reference);
    bindings_free(&bindings);

    Bindings legacy;
    bindings_init(&legacy);
    Atom *legacy_key = atom_var_like(a, p, UINT64_C(53999));
    CHECK(bindings_add_var(&legacy, legacy_key, atom_int(a, 0)) &&
          bindings_prepare_logical_write(&legacy), "construct distinct contextual variable");
    legacy.entries[0].value = binding_value_from_context(atom_expr3(a, head, y, y), 432u);
    bindings_invalidate_after_key_rewrite(&legacy);
    CHECK(bindings_apply(&legacy, a, p) == p,
          "same spelling cannot capture an unrelated contextual variable");
    Atom *ground = atom_expr2(a, head, atom_int(a, 7));
    CHECK(bindings_apply(&legacy, a, ground) == ground,
          "contextual support retains shared ground observation");
    bindings_free(&legacy);
}

static void contextual_decoded_writes(Arena *a) {
    Atom *p = atom_var_with_id(a, "decoded-p", UINT64_C(54001));
    Atom *q = atom_var_with_id(a, "decoded-q", UINT64_C(54002));
    Atom *z = atom_var_with_id(a, "decoded-z", UINT64_C(54003));
    Atom *x = atom_var_with_id(a, "decoded-x", UINT64_C(54004));
    Atom *xa = atom_var_with_id(a, "decoded-x", var_epoch_id(x->var_id, 440u));
    Atom *xb = atom_var_with_id(a, "decoded-x", var_epoch_id(x->var_id, 441u));
    Atom *head = atom_symbol(a, "decoded-pair");
    Atom *source = atom_expr3(a, head, x, x);
    Atom *ground = atom_expr3(a, head, atom_int(a, 7), atom_int(a, 7));
    for (unsigned use_builder = 0u; use_builder < 2u; use_builder++) {
        Bindings seed, plain, copied;
        BindingsBuilder builder;
        bindings_init(&seed);
        CHECK(bindings_add_var(&seed, p, atom_int(a, 0)) &&
              bindings_add_var(&seed, q, atom_int(a, 0)) &&
              bindings_prepare_logical_write(&seed), "construct decoded contextual seed");
        seed.entries[0].value = binding_value_from_context(source, 440u);
        seed.entries[1].value = binding_value_from_context(source, 441u);
        bindings_invalidate_after_key_rewrite(&seed);
        CHECK(bindings_clone(&plain, &seed) && bindings_builder_init(&builder, &seed),
              "create decoded ordinary and builder branches");
        Bindings *current = use_builder ? &builder.current : &plain;
        BindingsActivationView frame;
        bindings_activation_view_init(&frame);
        VarId ids[] = {x->var_id};
        Atom *variables[] = {x};
        uint32_t initial_mark = bindings_builder_save(&builder);
        if (use_builder) {
            CHECK(bindings_activation_view_prepare(&frame, &builder, ids, variables,
                                                      1u, 440u, 0u),
                  "prepare an unbound live slot for a contextual write");

        }
        size_t before = arena_accounted_live_bytes(a);
        CHECK(use_builder ? match_atoms_builder(p, q, &builder) : match_atoms(p, q, &plain),
              "decoded matcher unifies one open skeleton under different lexical contexts");
        BindingValue alias = bindings_lookup_value_id(current, xa->var_id);
        CHECK(alias.kind == BINDING_VALUE_CONTEXTUAL && alias.skeleton == x &&
              alias.epoch == 441u && arena_accounted_live_bytes(a) == before,
              "decoded bind stores the other context without allocating a variable copy");
        if (use_builder) {
            BindingValue live;
            bool live_found = lookup_frame_value(
                current, xa->var_id, &live);
            CHECK(live_found &&
                  live.kind == BINDING_VALUE_CONTEXTUAL && live.skeleton == x &&
                  live.epoch == 441u, "live slot publication retains contextual write metadata");
        }
        CHECK(use_builder ? match_atoms_builder(z, p, &builder) : match_atoms(z, p, &plain),
              "decoded matching binds an open contextual expression");
        BindingValue closure = bindings_lookup_value_id(current, z->var_id);
        CHECK(closure.kind == BINDING_VALUE_CONTEXTUAL && closure.skeleton == source &&
              closure.epoch == 440u && arena_accounted_live_bytes(a) == before,
              "expression binding retains the original skeleton and lexical context");
        bindings_init(&copied);
        CHECK(bindings_try_merge(&copied, current) && bindings_eq(&copied, current) &&
              bindings_lookup_value_id(&copied, z->var_id).kind == BINDING_VALUE_CONTEXTUAL,
              "ordinary merge preserves contextual values");
        BindingsBuilder merged;
        CHECK(bindings_builder_init(&merged, &seed) &&
              bindings_builder_try_merge(&merged, current) &&
              bindings_eq(&merged.current, current), "builder merge preserves contextual values");
        bindings_builder_free(&merged);
        Atom *wrong = atom_expr3(a, head, atom_int(a, 7), atom_int(a, 8));
        CHECK(!(use_builder ? bindings_builder_add_var_fresh(&builder, z, wrong)
                           : bindings_add_var(&plain, z, wrong)) &&
              bindings_eq(current, &copied),
              "failed contextual rebind rolls back partial repeated-variable refinement");
        uint32_t mark = bindings_builder_save(&builder);
        CHECK(use_builder ? bindings_builder_add_var_fresh(&builder, z, ground)
                          : bindings_add_var(&plain, z, ground),
              "contextual rebind refines the actual destination frame");
        CHECK(atom_eq(bindings_apply(current, a, z), ground) &&
              bindings_lookup_value_id(current, xb->var_id).skeleton &&
              atom_eq(bindings_lookup_value_id(current, xb->var_id).skeleton, atom_int(a, 7)) &&
              !bindings_lookup_value_id(&copied, xb->var_id).skeleton,
              "refinement observes the right variable and leaves a captured sibling unchanged");
        if (use_builder) {
            bindings_builder_rollback(&builder, mark);
            CHECK(bindings_eq(&builder.current, &copied), "rollback restores contextual bindings");
            Atom *cycle = atom_expr2(a, atom_symbol(a, "decoded-box"), xa);
            CHECK(!bindings_builder_add_var_fresh(&builder, xb, cycle) &&
                  !bindings_has_loop(&builder.current) && bindings_eq(&builder.current, &copied),
                  "a later bind that would close a cycle through a stored contextual alias is refused");
            bindings_builder_rollback(&builder, mark);
            CHECK(!bindings_has_loop(&builder.current) && bindings_eq(&builder.current, &copied),
                  "rollback after a refused bind changes nothing");
        }
        if (use_builder) {
            bindings_builder_rollback(&builder, initial_mark);
            BindingValue live;
            CHECK(lookup_frame_value(current, xa->var_id, &live) &&
                  !live.skeleton &&
                  bindings_eq(current, &seed),
                  "rollback removes a published contextual slot and restores its owner state");
        }
        bindings_activation_view_free(&frame);
        bindings_free(&copied);
        bindings_builder_free(&builder);
        bindings_free(&plain);
        bindings_free(&seed);
    }
}

static Atom *contextual_constraint_transport(void *context, Atom *atom) {
    return atom_deep_copy(context, atom);
}

static void contextual_constraints(Arena *a) {
    Atom *p = atom_var_with_id(a, "constraint-p", UINT64_C(55001));
    Atom *q = atom_var_with_id(a, "constraint-q", UINT64_C(55002));
    Atom *x = atom_var_with_id(a, "constraint-x", UINT64_C(55003));
    Atom *y = atom_var_with_id(a, "constraint-y", UINT64_C(55004));
    Atom *xa = atom_var_with_id(a, "constraint-x", var_epoch_id(x->var_id, 450u));
    Atom *yb = atom_var_with_id(a, "constraint-y", var_epoch_id(y->var_id, 451u));
    Atom *wrong_x = atom_var_with_id(a, "constraint-x", var_epoch_id(x->var_id, 452u));
    Atom *head = atom_symbol(a, "constraint-box");
    Bindings seed, reference;
    bindings_init(&seed);
    bindings_init(&reference);
    CHECK(bindings_add_var(&seed, p, atom_int(a, 0)) &&
          bindings_add_var(&seed, q, atom_int(a, 0)) &&
          bindings_prepare_logical_write(&seed), "construct contextual constraint roots");
    seed.entries[0].value = binding_value_from_context(atom_expr2(a, head, x), 450u);
    seed.entries[1].value = binding_value_from_context(atom_expr2(a, head, y), 451u);
    bindings_invalidate_after_key_rewrite(&seed);
    CHECK(bindings_add_constraint(&seed, p, q) && seed.eq_len == 1u &&
          seed.constraints[0].lhs.kind == BINDING_VALUE_CONTEXTUAL &&
          seed.constraints[0].lhs.epoch == 450u && seed.constraints[0].rhs.epoch == 451u,
          "delayed equality retains independent contexts on both sides");
    CHECK(bindings_add_var(&reference, p, atom_expr2(a, head, xa)) &&
          bindings_add_var(&reference, q, atom_expr2(a, head, yb)) &&
          bindings_add_constraint(&reference, p, q) && bindings_eq(&seed, &reference),
          "contextual constraints equal an independently qualified oracle");
    CHECK(bindings_add_constraint(&seed, q, p) && seed.eq_len == 1u,
          "symmetric contextual constraints are deduplicated without losing context");
    Bindings projected, reference_projected, unrelated;
    CHECK(bindings_project_reachable(&seed, &xa, 1u, &projected) &&
          bindings_project_reachable(&reference, &xa, 1u, &reference_projected) &&
          bindings_eq(&projected, &reference_projected) && projected.eq_len == 1u,
          "constraint projection follows qualified support on both sides");
    CHECK(bindings_project_reachable(&seed, &wrong_x, 1u, &unrelated) && unrelated.eq_len == 0u,
          "constraint projection excludes the same source variable in another frame");
    CHECK(atom_eq(bindings_to_atom(a, &seed), bindings_to_atom(a, &reference)),
          "constraint serialization reifies effective identities");
    CHECK(atom_eq(bindings_apply_value(&projected, a, seed.constraints[0].lhs),
                  atom_expr2(a, head, xa)),
          "typed observation qualifies a constraint even with no binding rows");
    Arena destination;
    arena_init(&destination);
    Bindings transported;
    CHECK(bindings_transport_logical(&transported, &seed, contextual_constraint_transport, &destination) &&
          bindings_eq(&transported, &seed) &&
          transported.constraints[0].lhs.kind == BINDING_VALUE_CONTEXTUAL &&
          transported.constraints[0].rhs.epoch == 451u &&
          arena_owns_ptr(&destination, transported.constraints[0].lhs.skeleton),
          "constraint transport changes syntax ownership while retaining lexical metadata");
    for (unsigned use_builder = 0u; use_builder < 2u; use_builder++) {
        Bindings plain, first, wrong, correct;
        BindingsBuilder builder;
        CHECK(bindings_clone(&plain, &seed) && bindings_builder_init(&builder, &seed),
              "create independent constraint refinement branches");
        bindings_init(&first); bindings_init(&wrong); bindings_init(&correct);
        CHECK(bindings_add_var(&first, xa, atom_int(a, 7)) &&
              bindings_add_var(&wrong, yb, atom_int(a, 8)) &&
              bindings_add_var(&correct, yb, atom_int(a, 7)),
              "prepare positive and negative constraint refinements");
        Bindings *current = use_builder ? &builder.current : &plain;
        uint32_t mark = bindings_builder_save(&builder);
        CHECK((use_builder ? bindings_builder_try_merge(&builder, &first)
                           : bindings_try_merge(&plain, &first)) && current->eq_len == 1u,
              "one refined side leaves its contextual equality pending");
        Bindings before_failure;
        CHECK(bindings_clone(&before_failure, current), "capture pending constraint before mismatch");
        bool wrong_rejected = !(use_builder
            ? bindings_builder_try_merge(&builder, &wrong)
            : bindings_try_merge(&plain, &wrong));
        bool failure_rolled_back = bindings_eq(current, &before_failure);
        CHECK(wrong_rejected && failure_rolled_back,
              "inconsistent ground refinement is rejected with exact rollback");
        bool correct_merged = use_builder
            ? bindings_builder_try_merge(&builder, &correct)
            : bindings_try_merge(&plain, &correct);
        CHECK(correct_merged && current->eq_len == 0u &&
              atom_eq(bindings_apply(current, a, p), atom_expr2(a, head, atom_int(a, 7))),
              "equal effective ground values discharge the delayed equality");
        CHECK(seed.eq_len == 1u && !bindings_lookup_value_id(&seed, xa->var_id).skeleton,
              "constraint refinement leaves its sibling substitution unchanged");
        if (use_builder) {
            bindings_builder_rollback(&builder, mark);
            CHECK(bindings_eq(&builder.current, &seed), "rollback restores both contextual constraint sides");
        }
        bindings_free(&before_failure); bindings_free(&first); bindings_free(&wrong);
        bindings_free(&correct); bindings_builder_free(&builder); bindings_free(&plain);
    }
    Bindings alias;
    bindings_init(&alias);
    CHECK(bindings_add_var(&alias, p, atom_int(a, 0)) && bindings_prepare_logical_write(&alias),
          "construct a contextual variable at a constraint root");
    alias.entries[0].value = binding_value_from_context(x, 450u);
    bindings_invalidate_after_key_rewrite(&alias);
    CHECK(bindings_add_constraint(&alias, p, atom_int(a, 7)) &&
          bindings_lookup_value_id(&alias, xa->var_id).skeleton &&
          atom_eq(bindings_apply(&alias, a, p), atom_int(a, 7)) &&
          !bindings_lookup_value_id(&alias, x->var_id).skeleton,
          "constraint root refinement binds the effective variable rather than its source name");
    bindings_free(&alias); bindings_free(&transported); arena_free(&destination);
    bindings_free(&unrelated); bindings_free(&projected); bindings_free(&reference_projected);
    bindings_free(&reference); bindings_free(&seed);
}

static void contextual_deep_equality(Arena *a) {
    Atom *x = atom_var_with_id(a, "deep-value-x", UINT64_C(56001));
    Atom *qualified = atom_var_with_id(a, "deep-value-x", var_epoch_id(x->var_id, 460u));
    Atom *source = x, *reference = qualified;
    Atom *head = atom_symbol(a, "deep-value-box");
    for (unsigned i = 0u; i < 20000u; i++) {
        source = atom_expr2(a, head, source);
        reference = atom_expr2(a, head, reference);
    }
    CHECK(binding_value_equal(binding_value_from_context(source, 460u),
                              binding_value_from_atom(reference)),
          "deep contextual equality uses effective identities without materializing a copy");
    CHECK(!binding_value_equal(binding_value_from_context(source, 461u),
                               binding_value_from_atom(reference)),
          "deep contextual inequality reaches the distinct leaf identity");
}

static void contextual_sibling_restore(Arena *a) {
    Atom *head = atom_symbol(a, "context-pair");
    Atom *box = atom_symbol(a, "context-box");
    Atom *x = atom_var_with_id(a, "context-x", UINT64_C(45001));
    Atom *y = atom_var_with_id(a, "context-y", UINT64_C(45002));
    Atom *source = atom_expr3(a, head, x, y);
    Atom *xa = atom_var_with_id(a, "context-x", var_epoch_id(x->var_id, 101u));
    Atom *xb = atom_var_with_id(a, "context-x", var_epoch_id(x->var_id, 102u));
    Atom *ya = atom_var_with_id(a, "context-y", var_epoch_id(y->var_id, 101u));
    Atom *yb = atom_var_with_id(a, "context-y", var_epoch_id(y->var_id, 102u));
    Atom *z = atom_var_with_id(a, "context-z", var_epoch_id(UINT64_C(45003), 103u));
    VarId pair_ids[] = {x->var_id, y->var_id};
    VarId forwarded_ids[] = {UINT64_C(45003)};
    for (unsigned mismatch = 0u; mismatch < 2u; mismatch++) {
        BindingsBuilder direct, reference;
        Bindings seed;
        CHECK(bindings_builder_init(&direct, NULL), "contextual direct builder");
        CHECK(bindings_builder_register_contextual_frame(
                  &direct, pair_ids, 2u, 101u) &&
              bindings_builder_register_contextual_frame(
                  &direct, pair_ids, 2u, 102u) &&
              bindings_builder_register_contextual_frame(
                  &direct, forwarded_ids, 1u, 103u) &&
              bindings_builder_add_var_fresh(&direct, xa, atom_expr2(a, box, z)) &&
              bindings_builder_add_var_fresh(&direct, xb, atom_expr2(a, box, atom_int(a, 7))) &&
              bindings_builder_add_var_fresh(&direct, ya, atom_int(a, 11)) &&
              bindings_builder_add_var_fresh(&direct, yb, atom_int(a, mismatch ? 12 : 11)),
              "seed independent contexts and a forwarded third-context variable");
        CHECK(bindings_clone(&seed, &direct.current), "capture contextual seed");
        CHECK(bindings_builder_init(&reference, &seed), "materialized contextual oracle");
        uint32_t direct_mark = bindings_builder_save(&direct);
        uint32_t reference_mark = bindings_builder_save(&reference);
        Atom *left = bindings_apply_epoch_then_all(&reference.current, a, source, 101u, 0u);
        Atom *right = bindings_apply_epoch_then_all(&reference.current, a, source, 102u, 0u);
        bool reference_ok = left && right && match_atoms_builder(left, right, &reference);
        bool direct_ok = match_atoms_epoch_view_builder(
            source, 101u, 0u, source, &direct, a, 102u);
        CHECK(direct_ok == !mismatch && direct_ok == reference_ok,
              "queued siblings retain their contexts after a materialized alias is followed");
        CHECK(bindings_eq(&direct.current, &reference.current),
              "contextual and materialized matching produce the same substitution");
        CHECK(atom_eq(bindings_lookup_value_id(&direct.current, z->var_id).skeleton, atom_int(a, 7)),
              "forwarded variable is refined in its own context before the sibling test");
        bindings_builder_rollback(&direct, direct_mark);
        bindings_builder_rollback(&reference, reference_mark);
        CHECK(bindings_eq(&direct.current, &seed) &&
              bindings_eq(&reference.current, &seed) &&
              !bindings_lookup_value_id(&seed, z->var_id).skeleton,
              "success and late failure both roll back without changing the captured context");
        bindings_builder_free(&reference);
        bindings_free(&seed);
        bindings_builder_free(&direct);
    }
}

static void attempt_profile(Arena *a) {
    BindingsBuilder bb;
    Atom *f = atom_symbol(a, "AttF");
    Atom *u = atom_symbol(a, "AttU");
    Atom *v = atom_symbol(a, "AttV");
    Atom *x = atom_var_with_id(a, "att-x", UINT64_C(31001));
    CHECK(bindings_builder_init(&bb, NULL), "attempt profile builder");
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_builder(atom_expr2(a, f, u), atom_expr2(a, f, u), &bb),
          "identical constructors succeed");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT) >= 1u,
          "a unification attempt is counted");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_SUCCESS) >= 1u,
          "a successful attempt is counted");
    test_runtime_stats_reset_counters();
    CHECK(!match_atoms_builder(
              atom_expr2(a, f, u), atom_expr2(a, v, u), &bb),
          "distinct constructors fail");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL) >= 1u,
          "constructor mismatch fails at the head");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL) ==
              0u,
          "head failure is not a repeated-variable failure");
    bindings_builder_rollback(&bb, 0u);
    test_runtime_stats_reset_counters();
    CHECK(!match_atoms_builder(
              atom_expr3(a, f, x, x), atom_expr3(a, f, u, v), &bb),
          "repeated variable mismatch fails");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL) >=
              1u,
          "repeated-variable consistency failure is counted");
    bindings_builder_free(&bb);
}

static void attempt_outcomes_cover_matchers(Arena *a) {
    Atom *f = atom_symbol(a, "attempt-total-f");
    Atom *u = atom_symbol(a, "attempt-total-u");
    Atom *v = atom_symbol(a, "attempt-total-v");
    Atom *x = atom_var_with_id(a, "attempt-total-x", UINT64_C(31002));
    VarId ids[] = {x->var_id};
    for (unsigned mode = 0u; mode < 5u; mode++) {
        TermUniverse universe;
        Arena persistent;
        arena_init(&persistent);
        CHECK(term_universe_init_with_store_format(&universe,
                  mode == 4u ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
                             : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1),
              "initialize attempt accounting store");
        term_universe_set_persistent_arena(&universe, &persistent);
        for (unsigned outcome = 0u; outcome < 4u; outcome++) {
            BindingsBuilder bb;
            CHECK(bindings_builder_init(&bb, NULL), "initialize attempt accounting branch");
            Atom *pattern = atom_expr3(a, f, x, outcome == 3u ? x : u);
            Atom *query = atom_expr3(a, outcome == 1u ? v : f, u,
                                    outcome >= 2u ? v : u);
            CettaOpenPatternInstruction program[] = {
                {.source = pattern, .subtree_span = 4u, .variable_mask = 1u},
                {.source = f, .subtree_span = 1u},
                {.source = x, .subtree_span = 1u, .variable_mask = 1u},
                {.source = pattern->expr.elems[2], .subtree_span = 1u,
                 .variable_mask = outcome == 3u ? 1u : 0u},
            };
            CettaOpenPatternPlan plan = {
                .source = pattern, .kind = ATOM_EXPR, .child_count = 3u,
                .variable_ids = ids, .variable_mask = 1u,
                .linear_program = program, .linear_program_len = 4u,
            };
            AtomId stored = term_universe_store_atom_id(&universe, &persistent, pattern);
            test_runtime_stats_reset_counters();
            bool matched;
            if (mode == 0u)
                matched = match_atoms_builder(query, pattern, &bb);
            else if (mode == 1u)
                matched = match_atoms_epoch_builder(query, pattern, &bb, a, 551u);
            else if (mode == 2u)
                matched = match_atoms_epoch_builder_rule_local_linear(
                    query, pattern, &plan, &bb, a, 551u);
            else
                matched = match_atoms_atom_id_epoch(query, &universe, stored,
                                                   &bb.current, a, 551u);
            uint64_t attempts = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
            uint64_t success = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_SUCCESS);
            uint64_t head = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL);
            uint64_t repeated = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL);
            uint64_t after = test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_AFTER_BIND_FAIL);
            CHECK(matched == (outcome == 0u), "accounting preserves each expected matching result");
            CHECK(attempts > 0u && attempts == success + head + repeated + after,
                  "every decoded, epoch, linear and encoded matcher invocation has exactly one outcome");
            if (outcome == 2u)
                CHECK(after > 0u && repeated == 0u &&
                          bindings_has_bound_values(&bb.current),
                      "late constructor mismatch after a write remains a counted matching failure");
            if (outcome == 0u)
                CHECK(success == attempts, "linear and encoded successes enter the same accounting");
            bindings_builder_free(&bb);
        }
        term_universe_free(&universe);
        arena_free(&persistent);
    }
}

static Atom *unary_spine(Arena *a, Atom *head, Atom *leaf, unsigned depth) {
    Atom *term = leaf;
    unsigned i;
    for (i = 0u; i < depth; i++) {
        term = atom_expr2(a, head, term);
        if (!term)
            return NULL;
    }
    return term;
}

/* SubstitutionAlgebra: unify a persistent open head against a goal skeleton
 * plus its environment; force only at observation.  Both shipped worklists. */
static void skeleton_plus_environment(void) {
    HashConsTable hc;
    hashcons_init(&hc);
    g_hashcons = &hc;
    Arena interned;
    arena_init(&interned);
    arena_set_hashcons(&interned, &hc);
    Atom *pair = atom_symbol(&interned, "SkelPair");
    Atom *unary = atom_symbol(&interned, "SkelUnary");
    Atom *leaf = atom_symbol(&interned, "skel-leaf");
    Atom *x = atom_var_with_id(&interned, "skel-x", UINT64_C(41001));
    Atom *l = atom_var_with_id(&interned, "skel-l", UINT64_C(41002));
    Atom *r = atom_var_with_id(&interned, "skel-r", UINT64_C(41003));
    Atom *head = atom_expr3(&interned, pair, x, x);
    Atom *skeleton = atom_expr3(&interned, pair, l, r);
    Atom *spine_a = unary_spine(&interned, unary, leaf, 8u);
    Atom *spine_b = unary_spine(&interned, unary, leaf, 8u);
    CHECK(spine_a && spine_b && spine_a == spine_b,
          "interned closed spines share one node");
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb, NULL), "skeleton interned builder");
    CHECK(bindings_builder_add_var_fresh(&bb, l, spine_a) &&
              bindings_builder_add_var_fresh(&bb, r, spine_b),
          "environment binds both skeleton holes to the interned spine");
    size_t before = arena_accounted_live_bytes(&interned);
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_builder(head, skeleton, &bb),
          "decoded worklist matches the open head against skeleton plus env");
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton == spine_a,
          "decoded match stores the interned spine, not a copied instance");
    CHECK(arena_accounted_live_bytes(&interned) == before,
          "decoded skeleton match does not force an instantiated goal");
    uint64_t intern_hits = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_COMMIT) +
        test_runtime_stats_counter(
            CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_EQUAL);
    CHECK(intern_hits > 0u,
          "interned bound subterms still compare by identity");
    Atom *forced = bindings_apply(&bb.current, &interned, skeleton);
    CHECK(forced && forced != skeleton &&
              forced->kind == ATOM_EXPR && forced->expr.len == 3u &&
              forced->expr.elems[1] == spine_a &&
              forced->expr.elems[2] == spine_b,
          "observation forces the skeleton once, sharing interned children");
    bindings_builder_rollback(&bb, 0u);
    CHECK(bindings_builder_add_id_fresh(
              &bb, var_epoch_id(l->var_id, 31u), l->sym_id, spine_a) &&
              bindings_builder_add_id_fresh(
                  &bb, var_epoch_id(r->var_id, 31u), r->sym_id, spine_b),
          "epoch environment rebinds the interned spine under activation keys");
    before = arena_accounted_live_bytes(&interned);
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_epoch_view_builder_current(
              skeleton, 31u, 0u, head, &bb, &interned),
          "epoch worklist matches the open head against skeleton plus env");
    CHECK(bindings_lookup_value_id(&bb.current, x->var_id).skeleton == spine_a,
          "epoch match stores the interned spine");
    CHECK(arena_accounted_live_bytes(&interned) == before,
          "epoch skeleton match does not force an instantiated goal");
    intern_hits = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_COMMIT) +
        test_runtime_stats_counter(
            CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_EQUAL);
    CHECK(intern_hits > 0u,
          "epoch interned bound subterms still compare by identity");
    bindings_builder_rollback(&bb, 0u);
    CHECK(bindings_builder_add_id_fresh(
              &bb, var_epoch_id(l->var_id, 31u), l->sym_id, spine_a) &&
              bindings_builder_add_id_fresh(
                  &bb, var_epoch_id(r->var_id, 31u), r->sym_id, spine_b),
          "activation environment uses epoch keys for the skeleton holes");
    before = arena_accounted_live_bytes(&interned);
    CHECK(match_atoms_epoch_view_builder_rule_local(
              skeleton, 31u, 0u, head, &bb, &interned, 11u),
          "rule-local view matches skeleton plus env without forcing");
    CHECK(arena_accounted_live_bytes(&interned) == before,
          "rule-local view does not force an instantiated goal");
    CHECK(bindings_lookup_value_id(&bb.current, var_epoch_id(x->var_id, 11u)).skeleton ==
              spine_a,
          "rule-local view binds the head variable to the interned spine");
    Atom *other_leaf = atom_symbol(&interned, "skel-other");
    Atom *other = unary_spine(&interned, unary, other_leaf, 8u);
    CHECK(other && other != spine_a,
          "a distinct interned spine remains a distinct node");
    bindings_builder_rollback(&bb, 0u);
    CHECK(bindings_builder_add_var_fresh(&bb, l, spine_a) &&
              bindings_builder_add_var_fresh(&bb, r, other),
          "environment of unequal interned spines");
    CHECK(!match_atoms_builder(head, skeleton, &bb),
          "decoded worklist rejects interned spines of a different hash");
    bindings_builder_rollback(&bb, 0u);
    CHECK(bindings_builder_add_id_fresh(
              &bb, var_epoch_id(l->var_id, 31u), l->sym_id, spine_a) &&
              bindings_builder_add_id_fresh(
                  &bb, var_epoch_id(r->var_id, 31u), r->sym_id, other),
          "epoch environment of unequal interned spines");
    CHECK(!match_atoms_epoch_view_builder_current(
              skeleton, 31u, 0u, head, &bb, &interned),
          "epoch worklist rejects interned spines of a different hash");
    bindings_builder_free(&bb);
    g_hashcons = NULL;
    arena_set_hashcons(&interned, NULL);
    hashcons_free(&hc);
    arena_free(&interned);

    Arena local;
    arena_init(&local);
    arena_set_hashcons(&local, NULL);
    pair = atom_symbol(&local, "LocalPair");
    unary = atom_symbol(&local, "LocalUnary");
    leaf = atom_symbol(&local, "local-leaf");
    x = atom_var_with_id(&local, "local-x", UINT64_C(41011));
    l = atom_var_with_id(&local, "local-l", UINT64_C(41012));
    r = atom_var_with_id(&local, "local-r", UINT64_C(41013));
    head = atom_expr3(&local, pair, x, x);
    skeleton = atom_expr3(&local, pair, l, r);
    spine_a = unary_spine(&local, unary, leaf, 8u);
    spine_b = unary_spine(&local, unary, leaf, 8u);
    CHECK(spine_a && spine_b && spine_a != spine_b && atom_eq(spine_a, spine_b),
          "arena-local copies are equal by structure, not identity");
    CHECK(bindings_builder_init(&bb, NULL), "skeleton local builder");
    CHECK(bindings_builder_add_var_fresh(&bb, l, spine_a) &&
              bindings_builder_add_var_fresh(&bb, r, spine_b),
          "environment binds two equal-but-not-shared spines");
    before = arena_accounted_live_bytes(&local);
    CHECK(match_atoms_builder(head, skeleton, &bb),
          "decoded worklist matches equal-but-not-shared bound spines");
    CHECK(arena_accounted_live_bytes(&local) == before,
          "structural consistency does not copy either local spine");
    bindings_builder_rollback(&bb, 0u);
    CHECK(bindings_builder_add_id_fresh(
              &bb, var_epoch_id(l->var_id, 31u), l->sym_id, spine_a) &&
              bindings_builder_add_id_fresh(
                  &bb, var_epoch_id(r->var_id, 31u), r->sym_id, spine_b),
          "epoch environment of equal-but-not-shared spines");
    before = arena_accounted_live_bytes(&local);
    CHECK(match_atoms_epoch_view_builder_current(
              skeleton, 31u, 0u, head, &bb, &local),
          "epoch worklist matches equal-but-not-shared bound spines");
    CHECK(arena_accounted_live_bytes(&local) == before,
          "epoch structural consistency does not copy either local spine");
    bindings_builder_rollback(&bb, 0u);
    CHECK(bindings_builder_add_var_fresh(&bb, l, spine_a) &&
              bindings_builder_add_var_fresh(&bb, r, spine_a),
          "repeated environment holes share one local spine");
    CHECK(match_atoms_builder(head, skeleton, &bb) &&
              bindings_lookup_value_id(&bb.current, x->var_id).skeleton == spine_a,
          "decoded repeated-variable success is a pointer, not an instance");
    bindings_builder_free(&bb);
    arena_free(&local);
}

/* Query/activation leftover lookup is a slot of the attempt frame
 * (BindingDecision), not the hash.  An identifier outside the inventory
 * still consults the environment map. */
static void query_slot_nonoriginal(void) {
    Arena a;
    arena_init(&a);
    Atom *x = atom_var_with_id(&a, "qs-x", UINT64_C(61001));
    Atom *y = atom_var_with_id(&a, "qs-y", UINT64_C(61002));
    Atom *z = atom_var_with_id(&a, "qs-z", UINT64_C(61099));
    Atom *u = atom_symbol(&a, "qs-u");
    Atom *v = atom_symbol(&a, "qs-v");
    VarId ids[2] = { x->var_id, y->var_id };
    Atom *vars[2] = { x, y };
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb, NULL), "query-slot builder");
    CHECK(bindings_builder_register_contextual_frame(
              &bb, ids, 2u, 17u) &&
          bindings_builder_add_id_fresh(
              &bb, var_epoch_id(x->var_id, 17u), x->sym_id, y) &&
              bindings_builder_add_id_fresh(
                  &bb, var_epoch_id(y->var_id, 17u), y->sym_id, u),
          "x aliases y and y is bound to u in the attempt suffix");
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    CHECK(bindings_activation_view_prepare(
              &frame, &bb, ids, vars, 2u, 17u, 0u),
          "attempt frame over x and y");
    CHECK(match_atoms_activation_view_builder_current(
              x, &frame, u, &bb, &a),
          "non-original chain through the attempt frame unifies");
    CHECK(match_atoms_builder_with_attempt_frame(y, u, &bb, &frame),
          "decoded leftover with an attempt frame still unifies");
    bindings_builder_rollback(&bb, 0u);
    bindings_activation_view_free(&frame);

    CHECK(bindings_builder_register_contextual_frame(
              &bb, ids, 2u, 17u) &&
          bindings_builder_add_id_fresh(
              &bb, var_epoch_id(x->var_id, 17u), x->sym_id, z) &&
              bindings_builder_add_var_fresh(&bb, z, u),
          "x aliases a variable outside the attempt frame");
    bindings_activation_view_init(&frame);
    CHECK(bindings_activation_view_prepare(
              &frame, &bb, ids, vars, 2u, 17u, 0u),
          "attempt frame still only names x and y");
    CHECK(match_atoms_activation_view_builder_current(
              x, &frame, u, &bb, &a),
          "unknown leftover identifiers still consult the environment map");
    CHECK(!match_atoms_activation_view_builder_current(
              x, &frame, v, &bb, &a),
          "the hashed leftover still enforces constructor mismatch");
    bindings_activation_view_free(&frame);
    bindings_builder_free(&bb);
    arena_free(&a);
}

static void interned_ground_identity(void) {
    HashConsTable hc;
    hashcons_init(&hc);
    g_hashcons = &hc;
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, &hc);
    Atom *leaf = atom_symbol(&arena, "intern-leaf");
    Atom *head = atom_symbol(&arena, "InternUnary");
    Atom *first = leaf;
    Atom *second = leaf;
    unsigned depth;
    for (depth = 0u; depth < 8u; depth++) {
        first = atom_expr2(&arena, head, first);
        second = atom_expr2(&arena, head, second);
    }
    CHECK(first && second && first == second,
          "one intern table publishes one closed ground DAG");
    CHECK(first->arena_id == 0u &&
              (first->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) != 0u &&
              !atom_has_vars(first),
          "published interned ground is closed and variable-free");
    BindingsBuilder bb;
    CHECK(bindings_builder_init(&bb, NULL), "intern identity builder");
    test_runtime_stats_reset_counters();
    CHECK(match_atoms_builder(first, second, &bb) && bb.current.len == 0u,
          "decoded worklist accepts interned ground by identity");
    CHECK(match_atoms_epoch_view_builder_current(
              first, 0u, 0u, second, &bb, &arena) &&
              bb.current.len == 0u,
          "epoch worklist accepts interned ground by identity");
    uint64_t equal = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_COMMIT) +
        test_runtime_stats_counter(
            CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_EQUAL);
    CHECK(equal > 0u,
          "interned identity does not walk an interned closed DAG");
    Atom *other_leaf = atom_symbol(&arena, "intern-other");
    Atom *other = other_leaf;
    for (depth = 0u; depth < 8u; depth++)
        other = atom_expr2(&arena, head, other);
    CHECK(other && first != other,
          "distinct interned ground remains a distinct node");
    test_runtime_stats_reset_counters();
    CHECK(!match_atoms_builder(first, other, &bb),
          "decoded worklist rejects interned ground of a different hash");
    CHECK(!match_atoms_epoch_view_builder_current(
              first, 0u, 0u, other, &bb, &arena),
          "epoch worklist rejects interned ground of a different hash");
    uint64_t unequal = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_UNEQUAL);
    CHECK(unequal > 0u,
          "interned inequality is a decided intern-node mismatch");
    g_hashcons = NULL;
    arena_set_hashcons(&arena, NULL);
    Arena local;
    arena_init(&local);
    arena_set_hashcons(&local, NULL);
    Atom *local_a = atom_symbol(&local, "local-leaf");
    Atom *local_b = atom_symbol(&local, "local-leaf");
    Atom *local_left = atom_expr2(&local, atom_symbol(&local, "LocalUnary"),
                                  local_a);
    Atom *local_right = atom_expr2(&local, atom_symbol(&local, "LocalUnary"),
                                   local_b);
    CHECK(local_left && local_right && local_left != local_right &&
              local_left->arena_id != 0u,
          "arena-local copies are not interned");
    CHECK(match_atoms_builder(local_left, local_right, &bb),
          "open arena-local ground still matches by structure");
    bindings_builder_free(&bb);
    arena_free(&local);
    arena_free(&arena);
    g_hashcons = NULL;
    hashcons_free(&hc);
}


/* A stored candidate and a decoded query retain independent lexical contexts,
 * including contexts obtained after following a binding. */
static void contextual_stored_matching(Arena *a) {
    Atom *p = atom_var_with_id(a, "stored-p", UINT64_C(56001));
    Atom *x = atom_var_with_id(a, "stored-x", UINT64_C(56002));
    Atom *y = atom_var_with_id(a, "stored-y", UINT64_C(56003));
    Atom *z = atom_var_with_id(a, "stored-z", UINT64_C(56004));
    Atom *r = atom_var_with_id(a, "stored-r", UINT64_C(56005));
    Atom *s = atom_var_with_id(a, "stored-s", UINT64_C(56006));
    Atom *pair = atom_symbol(a, "stored-pair");
    Atom *box = atom_symbol(a, "stored-box");
    Atom *source = atom_expr3(a, pair, x, y);
    Atom *nested = atom_expr2(a, box, z);
    Atom *seven = atom_int(a, 7), *eleven = atom_int(a, 11);
    Atom *ground = atom_expr3(a, pair, atom_expr2(a, box, seven), eleven);
    Atom *wrong = atom_expr3(a, pair, atom_expr2(a, box, seven), atom_int(a, 12));
    TermUniverseStoreFormat formats[] = {
        TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1,
        TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
    };
    for (unsigned mode = 0; mode < 2u; mode++) {
        TermUniverse universe;
        Arena persistent;
        arena_init(&persistent);
        CHECK(term_universe_init_with_store_format(&universe, formats[mode]),
              "initialize contextual stored matcher format");
        term_universe_set_persistent_arena(&universe, &persistent);
        Bindings seed, branch;
        bindings_init(&seed);
        CHECK(bindings_add_var(&seed, p, seven) &&
              bindings_add_id(&seed, var_epoch_id(x->var_id, 470u), x->sym_id, seven) &&
              bindings_add_id(&seed, var_epoch_id(y->var_id, 470u), y->sym_id, eleven) &&
              bindings_add_id(&seed, var_epoch_id(z->var_id, 471u), z->sym_id, seven) &&
              bindings_prepare_logical_write(&seed), "construct nested stored matcher contexts");
        seed.entries[0].value = binding_value_from_context(source, 470u);
        CHECK(bindings_rewrite_value_id(&seed, var_epoch_id(x->var_id, 470u),
                  binding_value_from_context(nested, 471u)), "write nested stored context");
        bindings_invalidate_after_key_rewrite(&seed);
        AtomId good_id = term_universe_store_atom_id(&universe, &persistent, ground);
        AtomId bad_id = term_universe_store_atom_id(&universe, &persistent, wrong);
        CHECK(tu_hdr(&universe, good_id) && tu_hdr(&universe, bad_id),
              "stable stored fixtures exercise the encoded frontier");
        CHECK(bindings_clone(&branch, &seed), "clone stored matcher positive branch");
        CHECK(match_atoms_atom_id_epoch(p, &universe, good_id, &branch, a, 480u),
              "stored children preserve context after a nested binding changes context");
        CHECK(match_binding_value_atom_id_epoch(binding_value_from_context(source, 470u),
                  &universe, good_id, &branch, a, 480u),
              "typed stored entry point retains the caller's lexical context");
        CHECK(!match_atoms_atom_id_epoch(p, &universe, bad_id, &branch, a, 480u),
              "stored sibling mismatch is checked in the original context");
        bindings_free(&branch);

        CHECK(bindings_clone(&branch, &seed) &&
              bindings_add_id(&branch, var_epoch_id(r->var_id, 480u), r->sym_id, seven) &&
              bindings_add_id(&branch, var_epoch_id(s->var_id, 480u), s->sym_id, eleven) &&
              bindings_prepare_logical_write(&branch), "construct contextual right-side binding");
        CHECK(bindings_rewrite_value_id(&branch, var_epoch_id(r->var_id, 480u),
                  binding_value_from_context(nested, 471u)), "write stored right-side context");
        bindings_invalidate_after_key_rewrite(&branch);
        AtomId open_id = term_universe_store_atom_id(&universe, &persistent, atom_expr3(a, pair, r, s));
        CHECK(match_atoms_atom_id_epoch(p, &universe, open_id, &branch, a, 480u),
              "stored right-side lookup compares contextual values without dropping either context");
        bindings_free(&branch);

        CHECK(bindings_clone(&branch, &seed), "clone stored variable branch");
        AtomId variable_id = term_universe_store_atom_id(&universe, &persistent, r);
        CHECK(match_atoms_atom_id_epoch(p, &universe, variable_id, &branch, a, 481u),
              "stored unbound variable binds an open contextual left expression");
        BindingValue bound = bindings_lookup_value_id(&branch, var_epoch_id(r->var_id, 481u));
        CHECK(bound.skeleton == source && bound.kind == BINDING_VALUE_CONTEXTUAL && bound.epoch == 470u,
              "stored variable binding retains the original skeleton and context");
        bindings_free(&branch);

        Atom *stored_u = atom_var_with_id(
            a, "stored-u", UINT64_C(56007));
        Atom *stored_v = atom_var_with_id(
            a, "stored-v", UINT64_C(56008));
        Atom *open_candidate = atom_expr3(a, pair, stored_u, stored_v);
        AtomId open_candidate_id = term_universe_store_atom_id(
            &universe, &persistent, open_candidate);
        Atom *persistent_open_candidate = term_universe_get_atom(
            &universe, open_candidate_id);
        bindings_init(&branch);
        CHECK(persistent_open_candidate &&
              match_atoms_atom_id_epoch(
                  p, &universe, open_candidate_id,
                  &branch, a, 482u),
              "an unbound query variable captures an open stored candidate");
        BindingValue persistent_bound =
            bindings_lookup_value_id(&branch, p->var_id);
        CHECK(persistent_bound.skeleton == persistent_open_candidate &&
              persistent_bound.kind ==
                  BINDING_VALUE_CONTEXTUAL_PERSISTENT &&
              persistent_bound.epoch == 482u,
              "an open stored candidate remains persistent syntax plus context");
        CHECK(!match_atoms_atom_id_epoch(
                  atom_expr2(a, box, seven), &universe,
                  open_candidate_id, &branch, a, 483u),
              "a mismatching stored shape does not masquerade as a closure");
        bindings_free(&branch);

        bindings_init(&branch);
        CHECK(bindings_add_var(&branch, p, seven) && bindings_prepare_logical_write(&branch),
              "construct unbound contextual stored query");
        branch.entries[0].value = binding_value_from_context(source, 470u);
        bindings_invalidate_after_key_rewrite(&branch);
        CHECK(match_atoms_atom_id_epoch(p, &universe, good_id, &branch, a, 480u),
              "stored ground candidate binds contextual child variables");
        BindingValue bx = bindings_lookup_value_id(&branch, var_epoch_id(x->var_id, 470u));
        BindingValue by = bindings_lookup_value_id(&branch, var_epoch_id(y->var_id, 470u));
        CHECK(bx.skeleton && atom_eq(bx.skeleton, ground->expr.elems[1]) && by.skeleton && atom_eq(by.skeleton, eleven) &&
              !bindings_lookup_value_id(&branch, x->var_id).skeleton &&
              !bindings_lookup_value_id(&branch, y->var_id).skeleton,
              "stored matcher writes qualified keys and excludes source IDs");
        bindings_free(&branch);

        /* State cells use the decoded store representation. The cell outlives
         * its stored wrapper and is never interpreted by this matcher. */
        StateCell cell = {.value = seven};
        Atom *state = atom_state(a, &cell);
        Atom *state_source = atom_expr3(a, pair, state, x);
        Atom *state_ground = atom_expr3(a, pair, state, eleven);
        AtomId state_id = term_universe_store_atom_id(&universe, &persistent, state_ground);
        CHECK(state_id != CETTA_ATOM_ID_NONE && !tu_hdr(&universe, state_id),
              "state fixture exercises the decoded stored representation");
        bindings_init(&branch);
        CHECK(bindings_add_var(&branch, p, seven) && bindings_prepare_logical_write(&branch),
              "construct decoded stored candidate query");
        branch.entries[0].value = binding_value_from_context(state_source, 472u);
        bindings_invalidate_after_key_rewrite(&branch);
        CHECK(match_atoms_atom_id_epoch(p, &universe, state_id, &branch, a, 480u),
              "decoded stored fallback matches contextual query");
        BindingValue state_bound = bindings_lookup_value_id(&branch, var_epoch_id(x->var_id, 472u));
        CHECK(state_bound.skeleton && atom_eq(state_bound.skeleton, eleven),
              "decoded stored fallback retains the query context");
        bindings_free(&branch);
        bindings_free(&seed);
        term_universe_free(&universe);
        arena_free(&persistent);
    }
}


static void contextual_observation_without_binding(Arena *a) {
    Atom *p = atom_var_with_id(a, "hidden-p", UINT64_C(58001));
    Atom *x = atom_var_with_id(a, "hidden-x", UINT64_C(58002));
    Atom *head = atom_symbol(a, "hidden-box");
    Atom *source = atom_expr2(a, head, x);
    Atom *seven = atom_int(a, 7);
    uint32_t epoch = 510u;
    VarId qualified = var_epoch_id(x->var_id, epoch);
    Bindings b, snapshot;
    bindings_init(&b);
    CHECK(bindings_add_var(&b, p, seven) &&
          bindings_add_id(&b, qualified, x->sym_id, seven) &&
          bindings_prepare_logical_write(&b), "construct contextual observation branch");
    b.entries[0].value = binding_value_from_context(source, epoch);
    bindings_invalidate_after_key_rewrite(&b);
    CHECK(bindings_clone(&snapshot, &b), "retain observation branch snapshot");
    Atom *full = bindings_apply_value_without_id(&b, a, p->var_id,
        bindings_lookup_value_id(&b, p->var_id));
    CHECK(full && atom_eq(full, atom_expr2(a, head, seven)),
          "hiding the source key observes contextual children transitively");
    Atom *hidden = bindings_apply_value_without_id(&b, a, qualified,
        binding_value_from_context(source, epoch));
    CHECK(hidden && hidden->kind == ATOM_EXPR &&
          hidden->expr.elems[1]->kind == ATOM_VAR && hidden->expr.elems[1]->var_id == qualified,
          "hiding a qualified dependency preserves its unbound lexical identity");
    CHECK(bindings_eq(&b, &snapshot) && bindings_lookup_value_id(&b, qualified).skeleton == seven,
          "observation changes neither the source branch nor its retained version");
    Atom *missing = bindings_apply_value_without_id(&b, a, UINT64_C(58003),
        binding_value_from_context(source, epoch));
    CHECK(missing && atom_eq(missing, full), "absent hidden key retains full observation");
    Atom *empty = bindings_apply_value_without_id(NULL, a, p->var_id,
        binding_value_from_context(source, epoch));
    CHECK(empty && empty->expr.elems[1]->var_id == qualified,
          "empty observation still qualifies contextual syntax");
    bindings_free(&snapshot);
    CHECK(bindings_prepare_logical_write(&b), "own observation cycle fixture");
    CHECK(bindings_rewrite_value_id(&b, qualified, binding_value_from_atom(p)),
          "write contextual observation cycle through its slot");
    CHECK(bindings_has_loop(&b), "contextual observation fixture has a real cycle");
    Atom *cut = bindings_apply_value_without_id(&b, a, p->var_id,
        binding_value_from_context(x, epoch));
    CHECK(cut && cut->kind == ATOM_VAR && cut->var_id == p->var_id && bindings_has_loop(&b),
          "hiding one edge observes a cyclic source without changing it");
    bindings_free(&b);
    bindings_init(&b);
    CHECK(match_binding_values(binding_value_from_context(x, epoch),
              binding_value_from_atom(seven), &b), "typed public matching binds the effective left key");
    CHECK(!match_binding_values(binding_value_from_context(x, epoch),
              binding_value_from_atom(atom_int(a, 8)), &b), "typed public matching rejects inconsistent refinement");
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, &b), "typed builder starts from a retained substitution");
    CHECK(match_binding_values_builder(binding_value_from_context(x, epoch + 1u),
              binding_value_from_atom(atom_int(a, 8)), &builder) &&
          bindings_lookup_value_id(&builder.current, qualified).skeleton == seven &&
          !bindings_lookup_value_id(&b, var_epoch_id(x->var_id, epoch + 1u)).skeleton,
          "typed builder keeps independent contexts and the parent version separate");
    bindings_builder_rollback(&builder, 0u);
    CHECK(bindings_eq(&b, &builder.current), "typed public builder rolls back to the parent substitution");
    bindings_builder_free(&builder);
    bindings_free(&b);

    bindings_init(&b);
    Atom *eight = atom_int(a, 8);
    CHECK(bindings_add_var(&b, x, seven) &&
          bindings_add_var(&b, p, eight) && bindings_prepare_logical_write(&b),
          "construct raw unframed duplicate-key observation fixture");
    b.entries[1].var_id = x->var_id;
    bindings_invalidate_after_key_rewrite(&b);
    Atom *duplicate_hidden = bindings_apply_value_without_id(&b, a, x->var_id,
        binding_value_from_atom(x));
    CHECK(duplicate_hidden && duplicate_hidden->kind == ATOM_VAR &&
          duplicate_hidden->var_id == x->var_id && b.len == 2u &&
          bindings_lookup_value_id(&b, x->var_id).skeleton == eight,
          "hiding a logical key hides every transported duplicate without changing the source");
    bindings_free(&b);
}


static void contextual_exact_root_policy(Arena *a) {
    Atom *x = atom_var_with_id(a, "exact-source", UINT64_C(59001));
    Atom *shadow = atom_var_with_id(a, "exact-source", UINT64_C(59002));
    Atom *p = atom_var_with_id(a, "exact-alias", UINT64_C(59003));
    Atom *y = atom_var_with_id(a, "exact-child", UINT64_C(59004));
    Atom *source = atom_expr2(a, atom_symbol(a, "exact-box"), y);
    Atom *zero = atom_int(a, 0);
    Bindings b;
    bindings_init(&b);
    CHECK(bindings_add_var(&b, shadow, zero) && bindings_add_var(&b, p, zero) &&
          bindings_prepare_logical_write(&b), "construct independent same-spelling root fixture");
    b.entries[0].value = binding_value_from_context(source, 520u);
    b.entries[1].value = binding_value_from_context(x, 521u);
    bindings_invalidate_after_key_rewrite(&b);
    BindingValue result;
    CHECK(bindings_resolve_value_exact(&b, binding_value_from_atom(x), &result) &&
          result.skeleton == x && result.kind == BINDING_VALUE_MATERIALIZED,
          "exact type-root lookup excludes a same-spelled distinct variable");
    CHECK(bindings_resolve_value_preview(&b, binding_value_from_atom(x), &result) &&
          result.skeleton == x && result.kind == BINDING_VALUE_MATERIALIZED,
          "ordinary root observation uses the same exact identity contract");
    CHECK(bindings_resolve_value_exact(&b, binding_value_from_atom(p), &result) &&
          result.skeleton == x && result.kind == BINDING_VALUE_CONTEXTUAL && result.epoch == 521u,
          "exact aliases retain the unbound target context without spelling capture");
    CHECK(bindings_prepare_logical_write(&b), "own exact contextual edge fixture");
    CHECK(match_binding_values(binding_value_from_context(x, 521u),
              binding_value_from_context(source, 520u), &b),
          "write exact qualified edge independently of spelling-based capture");
    CHECK(bindings_resolve_value_exact(&b, binding_value_from_atom(p), &result) &&
          result.skeleton == source && result.kind == BINDING_VALUE_CONTEXTUAL && result.epoch == 520u,
          "an exact qualified edge retains the value's independent context");
    Atom *observed = binding_value_materialize(a, result);
    CHECK(observed && observed->kind == ATOM_EXPR &&
          observed->expr.elems[1]->var_id == var_epoch_id(y->var_id, 520u),
          "structural type observation qualifies children in the resolved context");
    CHECK(bindings_rewrite_value_id(
              &b, var_epoch_id(x->var_id, 521u),
              binding_value_from_atom(p)),
          "rewrite the authoritative exact root slot into a cycle");
    CHECK(!bindings_resolve_value_exact(&b, binding_value_from_atom(p), &result),
          "cyclic exact aliases cannot establish a type root");
    bindings_free(&b);
}

static void contextual_root_preview(Arena *a) {
    Atom *p = atom_var_with_id(a, "preview-p", UINT64_C(57001));
    Atom *x = atom_var_with_id(a, "preview-x", UINT64_C(57002));
    Atom *q = atom_var_with_id(a, "preview-q", UINT64_C(57003));
    Atom *seven = atom_int(a, 7);
    Atom *source = atom_expr2(a, atom_symbol(a, "preview-box"), x);
    Bindings b;
    bindings_init(&b);
    CHECK(bindings_add_var(&b, p, seven) &&
          bindings_add_var(&b, q, seven) &&
          bindings_add_id(&b, var_epoch_id(x->var_id, 490u), x->sym_id, seven) &&
          bindings_prepare_logical_write(&b), "construct contextual root preview");
    b.entries[0].value = binding_value_from_context(source, 490u);
    b.entries[1].value = binding_value_from_context(x, 490u);
    bindings_invalidate_after_key_rewrite(&b);
    BindingValue result;
    CHECK(bindings_resolve_value_preview(&b, binding_value_from_atom(p), &result) &&
          result.skeleton == source && result.kind == BINDING_VALUE_CONTEXTUAL && result.epoch == 490u,
          "root preview retains an open expression's lexical context");
    Atom *target = seven;
    CHECK(bindings_resolve_term_view_root_v1(&b, p, &target) == CETTA_GSLT_TERM_VIEW_DEFER_V1 && !target,
          "Atom-only root observer declines an open contextual result");
    CHECK(bindings_resolve_value_preview(&b, binding_value_from_atom(q), &result) && result.skeleton == seven &&
          bindings_resolve_term_view_root_v1(&b, q, &target) == CETTA_GSLT_TERM_VIEW_OK_V1 && target == seven,
          "root preview follows qualified aliases to a ground value");
    CHECK(bindings_resolve_value_preview(&b, binding_value_from_context(x, 491u), &result) &&
          result.skeleton == x && result.epoch == 491u,
          "same source variable in another context remains unbound");
    CHECK(bindings_resolve_value_preview(NULL, binding_value_from_context(source, 492u), &result) &&
          result.skeleton == source && result.epoch == 492u,
          "empty substitution preserves a contextual root");
    CHECK(bindings_prepare_logical_write(&b), "own rows for cyclic alias fixture");
    b.entries[0].value = binding_value_from_atom(q);
    b.entries[1].value = binding_value_from_atom(p);
    bindings_invalidate_after_key_rewrite(&b);
    CHECK(!bindings_resolve_value_preview(&b, binding_value_from_atom(p), &result) &&
          bindings_resolve_term_view_root_v1(&b, p, &target) == CETTA_GSLT_TERM_VIEW_DEFER_V1 && !target,
          "cyclic aliases cannot certify a root observation");
    bindings_free(&b);
}

static void exclusive_candidate_frame(Arena *a) {
    Atom *slot = atom_var_with_id(
        a, "exclusive-slot", UINT64_C(61001));
    Atom *caller = atom_var_with_id(
        a, "exclusive-caller", UINT64_C(61002));
    Atom *seven = atom_int(a, 7);
    Atom *eight = atom_int(a, 8);
    VarId source_ids[] = {slot->var_id};
    Atom *source_variables[] = {slot};
    BindingsFrameSchema *schema =
        bindings_frame_schema_new_presented(
            source_ids, source_variables, 1u);
    BindingsExclusiveFrame *frame = bindings_exclusive_frame_new();
    BindingsBuilder builder;
    CHECK(schema && frame && bindings_builder_init(&builder, NULL),
          "construct exclusive candidate frame fixture");

    CHECK(bindings_exclusive_frame_begin(frame, schema, 601u) &&
          match_atoms_epoch_builder_rule_local_in_exclusive_frame(
              seven, slot, NULL, &builder, a, 601u, frame, false),
          "candidate-local slot accepts a successful rule binding");
    CHECK(builder.current.len == 0u &&
          !bindings_exclusive_frame_has_external_writes(frame),
          "candidate-local rule binding publishes neither row nor schema");
    Atom *observed = bindings_apply_exclusive_frame_then_all(
        &builder, a, slot, frame);
    CHECK(observed == seven,
          "candidate-local observation reads the authoritative direct slot");
    CHECK(bindings_exclusive_frame_freeze(frame, &builder) &&
          builder.current.len == 0u &&
          bindings_lookup_value_id(
              &builder.current,
              var_epoch_id(slot->var_id, 601u)).skeleton == seven,
          "escape publishes one exact authoritative slot");

    bindings_builder_rollback(&builder, 0u);
    CHECK(bindings_exclusive_frame_begin(frame, schema, 602u) &&
          !match_atoms_epoch_builder_rule_local_in_exclusive_frame(
              seven, eight, NULL, &builder, a, 602u, frame, false) &&
          builder.current.len == 0u,
          "failed candidate leaves the shared substitution untouched");

    CHECK(bindings_exclusive_frame_begin(frame, schema, 603u) &&
          match_atoms_epoch_builder_rule_local_in_exclusive_frame(
              caller, seven, NULL, &builder, a, 603u, frame, false) &&
          bindings_exclusive_frame_has_external_writes(frame) &&
          builder.current.len == 0u,
          "caller-variable writes remain private until the candidate escapes");
    CHECK(bindings_exclusive_frame_freeze(frame, &builder) &&
          bindings_lookup_value_id(
              &builder.current, caller->var_id).skeleton == seven,
          "freezing publishes externally visible caller-variable writes");

    bindings_builder_rollback(&builder, 0u);
    uint32_t slot_mark = bindings_builder_save(&builder);
    uint64_t write_before =
        bindings_builder_frame_write_boundary(&builder);
    CHECK(bindings_exclusive_frame_begin(frame, schema, 604u) &&
          match_atoms_epoch_builder_rule_local_in_exclusive_frame(
              seven, slot, NULL, &builder, a, 604u, frame, false) &&
          bindings_exclusive_frame_publish_slots(frame, &builder) &&
          builder.current.len == 0u &&
          bindings_lookup_value_id(
              &builder.current,
              var_epoch_id(slot->var_id, 604u)).skeleton == seven &&
          bindings_apply_epoch_then_all(
              &builder.current, a, slot, 604u, 0u) == seven &&
          bindings_builder_frame_write_boundary(&builder) > write_before,
          "authoritative publication writes and observes a versioned slot without a row");
    Bindings observed_reference;
    bindings_init(&observed_reference);
    CHECK(bindings_add_id(
              &observed_reference,
              var_epoch_id(slot->var_id, 604u), slot->sym_id, seven) &&
          atom_eq(bindings_to_atom(a, &builder.current),
                  bindings_to_atom(a, &observed_reference)),
          "ordered observation includes a frame-only authoritative value");
    bindings_free(&observed_reference);
    Bindings captured;
    bindings_init(&captured);
    CHECK(bindings_clone(&captured, &builder.current) &&
          bindings_lookup_value_id(
              &captured,
              var_epoch_id(slot->var_id, 604u)).skeleton == seven,
          "captured substitution retains authoritative frame-only values");
    bindings_free(&captured);
    BindingsEpochRoot slot_root = {
        .atom = slot,
        .epoch = 604u,
        .variable_support = NULL,
    };
    uint32_t compact_slot_mark = slot_mark;
    CHECK(bindings_builder_compact_reachable_with_epoch_roots(
              &builder, NULL, 0u, &slot_root, 1u,
              &compact_slot_mark, 1u, NULL, NULL) &&
          builder.current.len == 0u &&
          bindings_lookup_value_id(
              &builder.current,
              var_epoch_id(slot->var_id, 604u)).skeleton == seven,
          "compaction retains a live authoritative slot and its rollback state");
    slot_mark = compact_slot_mark;
    bindings_builder_rollback(&builder, slot_mark);
    CHECK(!bindings_lookup_value_id(
              &builder.current,
              var_epoch_id(slot->var_id, 604u)).skeleton &&
          builder.current.len == 0u,
          "rollback restores an authoritative frame-only slot without rows");

    uint32_t retained_mark = bindings_builder_save(&builder);
    CHECK(bindings_exclusive_frame_begin(frame, schema, 605u) &&
          match_atoms_epoch_builder_rule_local_in_exclusive_frame(
              seven, slot, NULL, &builder, a, 605u, frame, false) &&
          bindings_exclusive_frame_publish_slots(frame, &builder),
          "publish a frame-only value before a schema-extension branch");
    uint32_t extension_mark = bindings_builder_save(&builder);
    VarId initial_extension_ids[] = {slot->var_id};
    VarId complete_extension_ids[] = {
        slot->var_id, caller->var_id,
    };
    VarId extension_value_id = var_epoch_id(slot->var_id, 606u);
    CHECK(bindings_register_contextual_frame(
              &builder.current, initial_extension_ids, 1u, 606u) &&
          bindings_builder_add_id_fresh(
              &builder, extension_value_id, slot->sym_id, eight) &&
          bindings_register_contextual_frame(
              &builder.current, complete_extension_ids, 2u, 606u),
          "extending a live schema preserves the recorded slot write");
    bindings_builder_rollback(&builder, extension_mark);
    CHECK(bindings_lookup_value_id(
              &builder.current,
              var_epoch_id(slot->var_id, 605u)).skeleton == seven &&
          !bindings_lookup_value_id(
              &builder.current, extension_value_id).skeleton,
          "schema extension rollback preserves earlier frame-only history");
    bindings_builder_rollback(&builder, retained_mark);
    CHECK(!bindings_lookup_value_id(
              &builder.current,
              var_epoch_id(slot->var_id, 605u)).skeleton,
          "an earlier rollback still reaches frame-only history after extension");

    VarId projection_value_id = var_epoch_id(slot->var_id, 607u);
    uint32_t projection_mark = bindings_builder_save(&builder);
    CHECK(bindings_register_contextual_frame(
              &builder.current, initial_extension_ids, 1u, 607u) &&
          bindings_builder_add_id_fresh(
              &builder, projection_value_id, slot->sym_id, seven),
          "construct an authoritative framed write for projection");
    Atom *projection_root = atom_var_like(
        a, slot, projection_value_id);
    uint32_t compact_projection_mark = projection_mark;
    CHECK(projection_root &&
          bindings_builder_compact_reachable(
              &builder, &projection_root, 1u,
              &compact_projection_mark, 1u, NULL, NULL) &&
          bindings_lookup_value_id(
              &builder.current, projection_value_id).skeleton == seven,
          "projection preserves the logical version of a framed slot");
    bindings_builder_rollback(&builder, compact_projection_mark);
    CHECK(!bindings_lookup_value_id(
              &builder.current, projection_value_id).skeleton,
          "projected frame-slot undo remains valid after compaction");

    bindings_builder_rollback(&builder, 0u);
    Atom *pair = atom_symbol(a, "exclusive-pair");
    Atom *triple = atom_symbol(a, "exclusive-triple");
    VarId caller_source_id = caller->var_id;
    CHECK(bindings_register_contextual_frame(
              &builder.current, &caller_source_id, 1u, 608u),
          "register the caller frame for a cross-frame rule alias");
    Atom *caller_608 = atom_var_like(
        a, caller, var_epoch_id(caller->var_id, 608u));
    Atom *pair_query = atom_expr3(a, pair, caller_608, seven);
    Atom *pair_pattern = atom_expr3(a, pair, slot, slot);
    CHECK(caller_608 &&
          bindings_exclusive_frame_begin(frame, schema, 609u) &&
          match_binding_value_epoch_builder_rule_local_in_exclusive_frame(
              binding_value_from_atom(pair_query), pair_pattern, NULL,
              &builder, a, 609u, frame, false) &&
          bindings_exclusive_frame_has_external_writes(frame) &&
          !bindings_lookup_value_id(
              &builder.current, caller_608->var_id).skeleton,
          "a repeated rule slot refines the caller frame without changing ownership");
    CHECK(bindings_exclusive_frame_freeze(frame, &builder) &&
          bindings_lookup_value_id(
              &builder.current, caller_608->var_id).skeleton == seven,
          "freezing publishes the cross-frame refinement to its caller slot");

    bindings_builder_rollback(&builder, 0u);
    CHECK(bindings_register_contextual_frame(
              &builder.current, &caller_source_id, 1u, 610u),
          "register the caller frame for a failed cross-frame refinement");
    Atom *caller_610 = atom_var_like(
        a, caller, var_epoch_id(caller->var_id, 610u));
    Atom *triple_query = atom_expr(
        a, (Atom *[]){triple, caller_610, seven, eight}, 4u);
    Atom *triple_pattern = atom_expr(
        a, (Atom *[]){triple, slot, slot, slot}, 4u);
    CHECK(caller_610 &&
          bindings_exclusive_frame_begin(frame, schema, 611u) &&
          !match_binding_value_epoch_builder_rule_local_in_exclusive_frame(
              binding_value_from_atom(triple_query), triple_pattern, NULL,
              &builder, a, 611u, frame, false) &&
          bindings_exclusive_frame_has_external_writes(frame) &&
          !bindings_lookup_value_id(
              &builder.current, caller_610->var_id).skeleton,
          "a later mismatch leaves cross-frame refinement private to the failed candidate");

    bindings_builder_free(&builder);
    bindings_exclusive_frame_free(frame);
    bindings_frame_schema_release(schema);
}

int main(int argc, char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols; g_hashcons=NULL;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    Arena arena; arena_init(&arena);
    test_runtime_stats_reset_counters();
    exercise(&arena,false); exercise(&arena,true); epoch_separation(&arena); plan_validation(&arena); plan_head_order(&arena); planned_cycle_support_owns_context(&arena); unplanned_activation_owns_complete_context(&arena); test_nested_head_dereferences(&arena);
    test_type_wildcards(&arena);
    test_unary_spine(&arena);
    test_epoch_binding_keys();
    uint64_t elided=test_runtime_stats_counter(CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
    if (argc>1 && strcmp(argv[1],"--expect-elision")==0)
        CHECK(elided>0u,"candidate executes measured frame elision");
    fprintf(stderr,"elided_worklist_frames=%llu\n",(unsigned long long)elided);
    interned_ground_identity();
    skeleton_plus_environment();
    contextual_sibling_restore(&arena);
    binding_value_context(&arena);
    materialized_frame_branch_lifetime(&arena);
    contextual_binding_dependencies(&arena);
    dense_contextual_values(&arena);
    contextual_observation_visibility(&arena);
    contextual_ordinary_substitution(&arena);
    contextual_decoded_writes(&arena);
    contextual_constraints(&arena);
    contextual_deep_equality(&arena);
    contextual_stored_matching(&arena);
    contextual_root_preview(&arena);
    contextual_exact_root_policy(&arena);
    contextual_observation_without_binding(&arena);
    exclusive_candidate_frame(&arena);
    attempt_profile(&arena);
    attempt_outcomes_cover_matchers(&arena);
    query_slot_nonoriginal();
    printf("MatchWorklistFrames checks=%u failures=%u\n",checks,failures);
    arena_free(&arena); bindings_thread_cache_free(); g_var_intern=NULL; var_intern_free(&vars);
    g_symbols=NULL; symbol_table_free(&symbols);
    return failures?1:0;
}
