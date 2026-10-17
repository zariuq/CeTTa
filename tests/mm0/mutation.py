#!/usr/bin/env python3
"""Mutation qualification of the native MM0 checks.

Each mutant weakens one check of the verifier, or strengthens one so that
valid proofs fail.  A mutant is killed when the regression tests fail on
it: acceptance of the HOL and Peano fixtures, test_mm0, test_boundary,
test_dummy_identity and test_mmb (in that order; the first failure kills).  Module mutants, and
with --host every surviving semantic-route mutant, are built into a copy of
the CeTTa tree and run through test_host.py.

    python3 tests/mm0/mutation.py [--jobs N] [--only NAME ...] [--host]

The core stage compiles with $CC (default cc); --host runs `make` in a copy
of this tree with the flags given by --make-flags.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[2]
MM0 = ROOT / "native" / "mm0"
TESTS = ROOT / "tests" / "mm0"
FIX = TESTS / "fixtures"
CORE = ["mm0_base.c", "mm0_mmb.c", "mm0_spec.c", "mm0_semantic.c"]

MMB, SPEC, SEM, MOD = "mm0_mmb.c", "mm0_spec.c", "mm0_semantic.c", "mm0_cetta_module.c"

# (name, file, old text, new text).  Each old text occurs once in its file.
MUTANTS = [
    # ---- MMB unification (mmb.md "Unification") ----
    ("mmb-uref-identity", MMB, 'ENSURE((MbExpr *)(v->uheap[c.data] & ~UHEAP_SAVED) == e, "unify failure at ref");', "(void)e;"),
    ("mmb-uref-bounds", MMB, 'ENSURE(c.data < v->uhp, "bad ref step");', 'ENSURE(c.data <= v->uhp, "bad ref step");'),
    ("mmb-uterm-app", MMB, 'ENSURE(e->app, "unify failure at term");', "(void)0;"),
    ("mmb-uterm-id", MMB, 'ENSURE(e->id == c.data, "unify failure at term");', "(void)0;"),
    ("mmb-udummy-mode", MMB, 'ENSURE(mode == U_DEF, "Dummy command not allowed in theorem statements");', "(void)0;"),
    ("mmb-udummy-variable", MMB, 'ENSURE(!x->app, "unify failure at dummy");', "(void)0;"),
    ("mmb-udummy-sort", MMB, 'ENSURE((x->type >> 56) == (0x80u | c.data), "unify failure at dummy");',
     'ENSURE(x->type >> 63, "unify failure at dummy");'),
    ("mmb-udummy-fresh", MMB, 'ENSURE((((MbExpr *)v->uheap[i])->type & deps) == 0,\n'
     '                       "dummy disjoint variable violation");', "(void)deps;"),
    ("mmb-udummy-saved-terms", MMB, "if (v->uheap[i] & UHEAP_SAVED) continue;\n"
     "                ENSURE((((MbExpr *)v->uheap[i])->type & deps) == 0,",
     "ENSURE((((MbExpr *)(v->uheap[i] & ~UHEAP_SAVED))->type & deps) == 0,"),
    ("mmb-uhyp-order", MMB, 'ENSURE(v->usp == 0, "unfinished unify stack");\n'
     '                ENSURE(v->hsp > 0, "hypothesis stack underflow");',
     'ENSURE(v->hsp > 0, "hypothesis stack underflow");'),
    ("mmb-unify-end-hyps", MMB, 'if (mode == U_THM_END) ENSURE(v->hsp == 0, "unfinished hypothesis stack");', "(void)0;"),
    ("mmb-unify-end-stack", MMB, 'ENSURE(v->usp == 0, "unfinished unify stack");\n            return true;',
     "return true;"),
    # ---- MMB expressions and V / FV (mmb.md "V", "FV") ----
    ("mmb-def-fv-subtract", MMB, "if (target & (UINT64_C(1) << j)) deps &= ~v->bound_deps[j];", "(void)j;"),
    ("mmb-def-fv-subtract-all", MMB, "if (target & (UINT64_C(1) << j)) deps &= ~v->bound_deps[j];",
     "deps &= ~v->bound_deps[j];"),
    ("mmb-def-ret-deps", MMB, "if (ret & (UINT64_C(1) << j)) accum |= v->bound_deps[j];", "(void)j;"),
    ("mmb-thm-vars-mm0c", MMB, "accum |= arg->type & MMB_TYPE_DEPS_MASK;",
     "if (!(arg_at(v, te.p_args, i) & MMB_TYPE_BOUND_BIT)) accum |= arg->type & MMB_TYPE_DEPS_MASK;"),
    ("mmb-sorts-bound", MMB, "return upper == 0 || (upper == 0x80 && (from & MMB_TYPE_BOUND_BIT));",
     "return upper == 0 || upper == 0x80;"),
    ("mmb-sorts-sort", MMB, "uint64_t upper = (from ^ to) >> 56;", "uint64_t upper = ((from ^ to) >> 56) & 0x80;"),
    ("mmb-sorts-bound-to-regular", MMB, "return upper == 0 || (upper == 0x80 && (from & MMB_TYPE_BOUND_BIT));",
     "return upper == 0;"),
    ("mmb-term-range", MMB, 'ENSURE(t < v->nterms, "term out of range");', "(void)0;"),
    # ---- MMB theorem application (mm0.md "Verification") ----
    ("mmb-thm-range", MMB, 'ENSURE(t < v->nthms, "theorem out of range");', "(void)0;"),
    ("mmb-thm-in-def", MMB, 'ENSURE(mode != MODE_DEF, "invalid opcode in def");', "(void)0;"),
    ("mmb-thm-bound-disjoint", MMB, 'ENSURE((((MbExpr *)v->uheap[j])->type & deps) == 0,\n'
     '                       "disjoint variable violation");', "(void)j;"),
    ("mmb-thm-regular-disjoint", MMB, "ENSURE((target & (UINT64_C(1) << j)) || (v->bound_deps[j] & deps) == 0,",
     "ENSURE(1 || (v->bound_deps[j] & deps) == 0,"),
    ("mmb-thm-regular-ignore-deps", MMB, "ENSURE((target & (UINT64_C(1) << j)) || (v->bound_deps[j] & deps) == 0,",
     "ENSURE((v->bound_deps[j] & deps) == 0,"),
    # ---- MMB proof commands (mmb.md "Proof Checking") ----
    ("mmb-ref-bounds", MMB, 'if (c.data >= v->hp) { fail(v, MM0_REFUSED, "bad ref step"); return NULL; }',
     'if (c.data > v->hp) { fail(v, MM0_REFUSED, "bad ref step"); return NULL; }'),
    ("mmb-convref-left", MMB, "if (ok && !(cell->e1 == e1 && cell->e2 == e2))", "if (ok && !(cell->e2 == e2))"),
    ("mmb-convref-right", MMB, "if (ok && !(cell->e1 == e1 && cell->e2 == e2))", "if (ok && !(cell->e1 == e1))"),
    ("mmb-dummy-sort-range", MMB, 'if (c.data >= v->nsorts) { fail(v, MM0_REFUSED, "bad dummy sort"); return NULL; }', ""),
    ("mmb-dummy-free", MMB, "if (v->sorts[c.data] & (MM0_SORT_STRICT | MM0_SORT_FREE)) {",
     "if (v->sorts[c.data] & MM0_SORT_STRICT) {"),
    ("mmb-dummy-strict", MMB, "if (v->sorts[c.data] & (MM0_SORT_STRICT | MM0_SORT_FREE)) {",
     "if (v->sorts[c.data] & MM0_SORT_FREE) {"),
    ("mmb-dummy-limit", MMB, "if (v->next_bv >= MMB_MAX_BOUND) {", "if (v->next_bv > MMB_MAX_BOUND) {"),
    ("mmb-dummy-fresh-bit", MMB, "UINT64_C(1) << v->next_bv,\n                          .id",
     "UINT64_C(1),\n                          .id"),
    ("mmb-hyp-in-def", MMB, 'if (mode == MODE_DEF) { fail(v, MM0_REFUSED, "invalid opcode in def"); return NULL; }', ""),
    ("mmb-hyp-provable", MMB, "if (ok && !(v->sorts[MMB_TYPE_SORT(e->type)] & MM0_SORT_PROVABLE))", "if (0)"),
    ("mmb-refl", MMB, 'if (ok && e1 != e2) ok = fail(v, MM0_REFUSED, "Refl unify failure");', "(void)0;"),
    ("mmb-cong-term", MMB, "if (ok && !(e1->app && e2->app && e1->id == e2->id))", "if (ok && !(e1->app && e2->app))"),
    ("mmb-unfold-def", MMB, 'if (!(te.sort & 0x80)) { ok = fail(v, MM0_REFUSED, "Unfold: not a definition"); break; }', ""),
    ("mmb-save-obligation", MMB, "if (SLOT_TAG(s) == SLOT_CO_CONV) {", "if (0) {"),
    ("mmb-sorry", MMB, 'fail(v, MM0_REFUSED, "proof uses Sorry");\n            return NULL;',
     "ok = pop_as(v, SLOT_EXPR, &e) && push(v, slot(e, SLOT_PROOF));\n            break;"),
    ("mmb-pop-tag", MMB, 'ENSURE(SLOT_TAG(s) == tag, "bad stack slot");', "(void)tag;"),
    # ---- MMB binders and statements ----
    ("mmb-load-reserved", MMB, 'ENSURE_FORMAT(!(ty & MMB_TYPE_RESERVED_BIT), "reserved type bit set");', "(void)0;"),
    ("mmb-load-sort-range", MMB, 'ENSURE(sort < v->nsorts, "bad binder sort");', "(void)0;"),
    ("mmb-load-bound-strict", MMB, 'ENSURE(!(v->sorts[sort] & MM0_SORT_STRICT), "bound variable in strict sort");', "(void)0;"),
    ("mmb-load-bound-deps", MMB, "ENSURE(v->next_bv < MMB_MAX_BOUND && deps == UINT64_C(1) << v->next_bv,",
     "ENSURE(v->next_bv < MMB_MAX_BOUND,"),
    ("mmb-load-regular-deps", MMB, 'ENSURE((deps >> v->next_bv) == 0, "bad binder deps");', "(void)0;"),
    ("mmb-term-pure", MMB, 'ENSURE(!(v->sorts[sort] & MM0_SORT_PURE), "term in pure sort");', "(void)0;"),
    ("mmb-term-ret-sort", MMB, 'ENSURE((ret >> 56) == sort, "bad return type");', "(void)0;"),
    ("mmb-def-stack-one", MMB, 'ENSURE(v->sp == 1, "stack has != one element");\n'
     '        ENSURE(SLOT_TAG(v->stack[0]) == SLOT_EXPR, "bad stack slot");',
     'ENSURE(SLOT_TAG(v->stack[0]) == SLOT_EXPR, "bad stack slot");'),
    ("mmb-def-value-type", MMB, 'ENSURE(sorts_compatible(val->type, ret), "type mismatch");', "(void)0;"),
    ("mmb-def-unaccounted", MMB,
     'ENSURE((val->type & MMB_TYPE_DEPS_MASK & ~ret) == 0, "type has unaccounted dependencies");', "(void)0;"),
    ("mmb-local-term", MMB, 'ENSURE_FORMAT(!local, "local term");', "(void)0;"),
    ("mmb-thm-next-statement", MMB, 'ENSURE_FORMAT((size_t)(end - v->file) == next_offset, "Next statement incorrect");\n'
     '    ENSURE(v->sp == 1', "ENSURE(v->sp == 1"),
    ("mmb-thm-stack-one", MMB, 'ENSURE(v->sp == 1, "stack has != one element");\n    bool is_thm', "bool is_thm"),
    ("mmb-thm-final-proof", MMB,
     'ENSURE(SLOT_TAG(v->stack[0]) == (is_thm ? SLOT_PROOF : SLOT_EXPR), "bad stack slot");', "(void)is_thm;"),
    ("mmb-concl-provable", MMB, "ENSURE(v->sorts[MMB_TYPE_SORT(val->type)] & MM0_SORT_PROVABLE,",
     "ENSURE(1 || (v->sorts[MMB_TYPE_SORT(val->type)] & MM0_SORT_PROVABLE),"),
    ("mmb-all-theorems", MMB, 'ENSURE_FORMAT(v->nthms == v->num_thms, "not all theorems proved");', "(void)0;"),

    # ---- specification matching (parser.c check_expr, with the dummy repair) ----
    ("spec-dummy-injective", SPEC, 'ENSURE(!s->uheap[i]->var || s->uheap[i]->head != e->head,\n'
     '                       "dummy matched more than once");', "(void)i;"),
    ("spec-expected-dummy", SPEC, 'ENSURE(e->head >= nargs, "expected a dummy");', "(void)0;"),
    ("spec-dummy-sort", SPEC, 'ENSURE(c.data == s->var_sorts[e->head], "unify failure at dummy");', "(void)0;"),
    ("spec-udummy-variable", SPEC, 'ENSURE(e->var, "store type error");', "(void)0;"),
    ("spec-uterm-variable", SPEC, 'ENSURE(!e->var, "store type error");', "(void)0;"),
    ("spec-uterm-head", SPEC, 'ENSURE(e->head == c.data, "unify failure at term");', "(void)0;"),
    ("spec-uref", SPEC, "CHECK(deep_eq(s, s->uheap[c.data], e));", "(void)e;"),
    ("spec-deep-eq-head", SPEC, 'ENSURE(x->var == y->var && x->head == y->head, "expression mismatch");',
     'ENSURE(x->var == y->var, "expression mismatch");'),
    ("spec-hyp-underflow", SPEC, 'ENSURE(nhyps > 0, "hypothesis number mismatch");', "(void)0;"),
    ("spec-hyp-unused", SPEC, 'ENSURE(nhyps == 0, "unfinished hypothesis stack");', "(void)0;"),
    ("spec-unify-end", SPEC, 'ENSURE(s->usp == 0, "unfinished unify stack");', "(void)0;"),
    ("spec-def-body", SPEC, "!check_expr(s, tables, nargs, args + 8 * ((size_t)nargs + 1), body, NULL, 0))", "0)"),
    # ---- specification statements (parser.c) ----
    ("spec-binder-type", SPEC, "ENSURE(type == (curly ? a & ~((UINT64_C(1) << 56) - 1) : a),\n"
     '                   "variable type does not match theorem");', "(void)a;"),
    ("spec-arrow-type", SPEC, 'ENSURE(type == mm0_le64(args + 8 * (size_t)arg_idx), "variable type does not match theorem");',
     "(void)0;"),
    ("spec-ret-type", SPEC, 'ENSURE(type == ret, "return type does not match theorem");', "(void)0;"),
    ("spec-arg-count", SPEC, 'ENSURE(arg_idx == nargs, "incorrect number of arguments");', "(void)0;"),
    ("spec-bound-deps", SPEC, 'ENSURE(!curly, "bound variables should not have dependencies");', "(void)0;"),
    ("spec-dummy-outside-def", SPEC, 'if (dummy) ENSURE(kind == B_DEF, "dummies are only allowed in defs");', ""),
    ("spec-shadowing", SPEC, 'ENSURE(!mm0_names_get(&s->vars, name.text, name.len, &existing), '
     '"variable shadowing is not allowed");', "(void)existing;"),
    ("spec-sort-mods", SPEC, 'if (mods != tables->sorts[index]) { fail(s, MM0_REFUSED, "sort modifiers do not match"); '
     "return error->status; }", ""),
    ("spec-term-is-def", SPEC, 'if (k == KW_TERM && is_def) { fail(s, MM0_REFUSED, "expecting a term"); return error->status; }', ""),
    ("spec-def-is-term", SPEC, 'if (k == KW_DEF && !is_def) { fail(s, MM0_REFUSED, "expecting a def"); return error->status; }', ""),
    ("spec-axiom-kind", SPEC, 'ENSURE(want == WANT_AXIOM, "expecting an axiom");', "(void)0;"),
    ("spec-theorem-kind", SPEC, 'ENSURE(want == WANT_THM, "expecting a theorem");', "(void)0;"),
    ("spec-duplicate-term", SPEC, "if (mm0_names_get(&s->terms, name.text, name.len, &existing)) {",
     "if (0 && mm0_names_get(&s->terms, name.text, name.len, &existing)) {"),
    ("spec-duplicate-theorem", SPEC, "if (mm0_names_get(&s->thms, name.text, name.len, &existing)) {",
     "if (0 && mm0_names_get(&s->thms, name.text, name.len, &existing)) {"),
    ("spec-nul", SPEC, 'if (!s->text_ok) return fail(s, MM0_MALFORMED, "Null character in input");', ""),
    # ---- specification notation (mm0.md "Notations") ----
    ("spec-notation-twice", SPEC, 'if (used[var]) { good = fail(s, MM0_REFUSED, "notation variable used twice"); break; }', ""),
    ("spec-notation-unused", SPEC, 'if (!used[i]) good = fail(s, MM0_REFUSED, "notation does not use every variable");',
     "(void)i;"),
    ("spec-prefix-twice", SPEC, 'ENSURE(s->tokens[token].prefix == NO_INDEX, "token already has a prefix notation");\n'
     "        Literal", "Literal"),
    ("spec-infix-twice", SPEC, 'ENSURE(s->tokens[token].infix == NO_INDEX, "token already has an infix notation");', "(void)0;"),
    ("spec-infix-binary", SPEC, 'ENSURE(nargs == 2, "expected a binary operator");', "(void)0;"),
    ("spec-precedence-mismatch", SPEC, 'ENSURE(t->prec == p, "precedence mismatch");', "(void)0;"),
    ("spec-associativity", SPEC, 'ENSURE(s->prec_assoc[i].rassoc == rassoc, "precedence level is both left and right assoc");',
     "(void)0;"),
    ("spec-coercion-type", SPEC, "ENSURE(term_arg_sort(tables, term, 0) == s1 &&", "ENSURE(1 || term_arg_sort(tables, term, 0) == s1 &&"),
    ("spec-coercion-cycle", SPEC, 'ENSURE(coe[(size_t)s2 * n + s1].tag == COE_EMPTY, "coercion cycle detected");', "(void)0;"),
    ("spec-coercion-diamond", SPEC, 'ENSURE(find(s->component, s1) != find(s->component, s2), "coercion diamond detected");',
     "(void)0;"),
    ("spec-coercion-provable", SPEC, 'ENSURE(s->prov[l] == NO_INDEX, "coercion diamond to provable detected");', "(void)0;"),

    # ---- semantic route (Derives, ConvWitness, FreshDummies, admission) ----
    ("sem-fits-bound", SEM, "return e->sort == b->sort && (!b->bound || (e->var && e->bound));", "return e->sort == b->sort;"),
    ("sem-fits-sort", SEM, "return e->sort == b->sort && (!b->bound || (e->var && e->bound));",
     "return !b->bound || (e->var && e->bound);"),
    ("sem-app-arity", SEM, 'if (nargs != t->nargs) { fail(d, MM0_REFUSED, "wrong number of arguments"); return NULL; }', ""),
    ("sem-vars-mm0c", SEM, "for (uint32_t w = 0; w < d->words; ++w) vars[w] |= args[k]->vars[w];",
     "if (!d->theory->terms[head].args[k].bound) for (uint32_t w = 0; w < d->words; ++w) vars[w] |= args[k]->vars[w];"),
    ("sem-bound-strict", SEM, 'if (bound) ENSURE(!(mods & MM0_SORT_STRICT), "bound variable in strict sort");', "(void)bound;"),
    ("sem-dummy-free", SEM, 'if (dummy) ENSURE(!(mods & (MM0_SORT_STRICT | MM0_SORT_FREE)), "dummy variable in strict or free sort");',
     'if (dummy) ENSURE(!(mods & MM0_SORT_STRICT), "dummy variable in strict or free sort");'),
    ("sem-binder-deps", SEM, 'ENSURE(deps[i] < d->nbinders && d->binders[deps[i]].bound, "expecting bound variable");',
     'ENSURE(deps[i] < d->nbinders, "expecting bound variable");'),
    ("sem-bound-binder-deps", SEM, 'ENSURE(!bound, "bound variables should not have dependencies");', "(void)0;"),
    ("sem-statement-provable", SEM,
     "ENSURE(e->sort < d->theory->nsorts && (d->theory->sorts[e->sort].mods & MM0_SORT_PROVABLE),",
     "ENSURE(e->sort < d->theory->nsorts,"),
    ("sem-statement-dummy", SEM, 'ENSURE(!mentions_dummy(d, e), "a statement may not use proof dummies");', "(void)0;"),
    ("sem-trans-middle", SEM, "ENSURE(x.right == y.left && x.sort == y.sort,",
     "ENSURE(x.sort == y.sort,"),
    ("sem-unfold-def", SEM, 'ENSURE((t->flags & MM0_DECL_DEF) && t->body, "unfold: not a definition");', "(void)0;"),
    ("sem-unfold-count", SEM, 'ENSURE(nimages == t->ndummies, "unfold: wrong number of dummy images");', "(void)0;"),
    ("sem-unfold-image-bound", SEM,
     "if (p >= d->nbinders || !d->binders[p].bound || d->binders[p].sort != t->dummy_sorts[k]) {",
     "if (p >= d->nbinders || d->binders[p].sort != t->dummy_sorts[k]) {"),
    ("sem-unfold-image-sort", SEM,
     "if (p >= d->nbinders || !d->binders[p].bound || d->binders[p].sort != t->dummy_sorts[k]) {",
     "if (p >= d->nbinders || !d->binders[p].bound) {"),
    ("sem-unfold-fresh", SEM, 'if (has_bit(values[j]->vars, p)) ok = fail(d, MM0_REFUSED, "unfold: a dummy image is not fresh");',
     "(void)p;"),
    ("sem-unfold-images-injective", SEM, "for (size_t j = 0; ok && j < (size_t)nargs + k; ++j)",
     "for (size_t j = 0; ok && j < (size_t)nargs; ++j)"),
    ("sem-claim", SEM, "ENSURE(claim == concl, \"the row's claim differs from its conclusion\");", "(void)concl;"),
    ("sem-row-hyp-range", SEM, 'if (hyp >= d->nhyps) fail(d, MM0_REFUSED, "no such hypothesis");\n        else add_row',
     "add_row"),
    ("sem-thm-disjoint", SEM, 'if (!depends) ENSURE(!has_bit(args[j]->vars, x), "disjoint variable violation");',
     "(void)depends;"),
    ("sem-thm-disjoint-self", SEM, "if (j == i) continue;", ""),
    ("sem-thm-nparents", SEM, 'ENSURE(nparents == t->nhyps, "wrong number of hypotheses");', "(void)nparents;"),
    ("sem-thm-parents", SEM, 'ENSURE(d->rows[parents[h]] == want, "parent row does not prove the hypothesis");',
     "(void)want;"),
    ("sem-conv-parent", SEM, "} else if (d->rows[parent] != c->left) {", "} else if (0) {"),
    # ---- the repaired boundaries (tests/mm0/test_boundary.c) ----
    ("sem-conv-issuer", SEM, "if (c->issuer != d->serial || c->index >= d->nconvs) {",
     "if (c->index >= d->nconvs) {"),
    ("sem-conv-index", SEM, "if (c->issuer != d->serial || c->index >= d->nconvs) {",
     "if (c->issuer != d->serial) {"),
    ("sem-node-owner", SEM, 'if (e->owner != d) return fail(d, MM0_FOREIGN, "an expression of another builder");',
     "(void)0;"),
    ("sem-row-step", SEM, "static bool add_row(Mm0Decl *d, Mm0Node *concl, Mm0Node *claim, uint32_t *row) {\n    CHECK(step(d));",
     "static bool add_row(Mm0Decl *d, Mm0Node *concl, Mm0Node *claim, uint32_t *row) {"),
    ("sem-witness-step", SEM, "static bool issue(Mm0Decl *d, Mm0Node *left, Mm0Node *right, uint32_t sort, Mm0Conv *out) {\n    CHECK(step(d));",
     "static bool issue(Mm0Decl *d, Mm0Node *left, Mm0Node *right, uint32_t sort, Mm0Conv *out) {"),
    ("sem-copy-step", SEM, "if (stack[sp - 1].next == 0 && !step(d)) { ok = false; break; }\n        if (!n->var && stack[sp - 1].next < n->nargs) {\n            Mm0Node *child = n->args[stack[sp - 1].next++];\n            if (!MM0_RESERVE(stack, cap_stack, sp + 1)) { ok = false; break; }\n            stack[sp++] = (NodeFrame){child, 0};\n            continue;\n        }\n        Mm0Expr *e",
     "if (!n->var && stack[sp - 1].next < n->nargs) {\n            Mm0Node *child = n->args[stack[sp - 1].next++];\n            if (!MM0_RESERVE(stack, cap_stack, sp + 1)) { ok = false; break; }\n            stack[sp++] = (NodeFrame){child, 0};\n            continue;\n        }\n        Mm0Expr *e"),
    ("sem-resume-final", SEM, "if (d->error.status != MM0_OK && d->error.status != MM0_LIMIT &&\n        d->error.status != MM0_INTERRUPTED)",
     "if (0)"),
    ("sem-ret-deps", SEM, 'ENSURE(ret_deps[i] < d->nordinary && d->binders[ret_deps[i]].bound, "expecting bound variable");',
     'ENSURE(ret_deps[i] < d->nordinary, "expecting bound variable");'),
    ("sem-term-pure", SEM, 'ENSURE(!(th->sorts[ret_sort].mods & MM0_SORT_PURE), "term in pure sort");', "(void)0;"),
    ("sem-def-body-sort", SEM, 'ENSURE(body->sort == ret_sort, "type mismatch");', "(void)0;"),
    ("sem-def-fv", SEM, 'ENSURE((fv[w] & ~positions[w]) == 0, "type has unaccounted dependencies");', "(void)0;"),
    ("sem-duplicate-name", SEM, 'if (name) ENSURE(!mm0_names_get(names, name, strlen(name), &existing), "duplicate name");',
     "(void)existing;"),
    ("sem-root-concl", SEM, 'ENSURE(d->rows[root] == d->concl, "the proof does not conclude the statement");', "(void)0;"),

    # ---- CeTTa module (owned handles; host stage only) ----
    ("mod-foreign-theorem", MOD, 'if (h->owner != c->theory) return fail(c, MM0_FOREIGN, "theorem of another MM0 theory");', ""),
    ("mod-same-declaration-owner", MOD, "h->owner == h2->owner && h->index == h2->index", "h->index == h2->index"),
    ("mod-publish-owner-ref", MOD, "++c->theory->refs;\n    atomic_fetch_add(&g_live_theorems, 1);",
     "atomic_fetch_add(&g_live_theorems, 1);"),
    ("mod-held-release", MOD, "theory_unref(h->owner);\n    free(h);", "free(h);"),
    ("mod-duplicate-row", MOD, 'if (s->used) return fail(c, MM0_REFUSED, "duplicate proof row ID");', ""),
]


# Mutants that cannot change a verdict, and why.  A surviving mutant that is
# not listed here is a gap in the tests.
EQUIVALENT = {
    "mmb-udummy-variable":
        "the sort test after it needs the bound bit, which an application's type never has",
    "spec-dummy-sort":
        "a dummy first occurs at a bound binder position (otherwise it is free in the value and the "
        "MMB check refuses the definition); there both the specification's typing and the MMB check "
        "fix its sort to the binder's",
    "spec-udummy-variable":
        "the same position is a bound binder position, where the specification's typing admits only variables",
    "spec-unify-end":
        "the specification matcher runs the stream the MMB check accepted, with the same arities, so "
        "its stack empties exactly when the MMB check's does",
}


def run(cmd, cwd=None, stdin=None, timeout=300):
    try:
        with open(stdin, "rb") if stdin else open(os.devnull, "rb") as f:
            p = subprocess.run(cmd, cwd=cwd, stdin=f, capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).decode(errors="replace")
    except subprocess.TimeoutExpired:
        return "timeout", ""


def mutate(name, file, old, new, dest):
    text = (MM0 / file).read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{name}: the mutation site occurs {count} times in {file}")
    dest.write_text(text.replace(old, new))


class Core:
    """Objects of the unmutated sources, built once; a mutant recompiles
    one file and relinks."""

    def __init__(self, work, cc, cflags):
        self.work, self.cc, self.cflags = work, cc, cflags
        self.base = work / "base"
        self.base.mkdir()
        sources = [MM0 / f for f in CORE] + [TESTS / "mm0_check.c", TESTS / "test_mm0.c",
                                             TESTS / "test_boundary.c"]
        with ThreadPoolExecutor(os.cpu_count() or 2) as pool:
            for rc, out in pool.map(lambda src: self.compile(src, self.base / (src.stem + ".o")), sources):
                if rc != 0:
                    raise SystemExit("baseline build failed:\n" + out)

    def compile(self, src, obj):
        return run(self.cc + self.cflags + ["-I", str(MM0), "-c", str(src), "-o", str(obj)])

    def build(self, mdir, mutated):
        objs = []
        for f in CORE:
            if f == mutated:
                obj = mdir / (Path(f).stem + ".o")
                rc, out = self.compile(mdir / f, obj)
                if rc != 0:
                    return None, out
                objs.append(str(obj))
            else:
                objs.append(str(self.base / (Path(f).stem + ".o")))
        for prog in ("mm0_check", "test_mm0", "test_boundary"):
            rc, out = run(self.cc + [str(self.base / (prog + ".o"))] + objs + ["-o", str(mdir / prog)])
            if rc != 0:
                return None, out
        return mdir, ""


def core_tests(bindir):
    """The core regression tests, cheapest first.  Returns the name of the
    first failing test, or None."""
    check, unit = str(bindir / "mm0_check"), str(bindir / "test_mm0")
    tests = [
        ("hol-fixture", [check, str(FIX / "hol.mmb")], FIX / "hol.mm0"),
        ("peano-fixture", [check, str(FIX / "peano.mmb")], FIX / "peano.mm0"),
        ("test_mm0", [unit, str(FIX / "hol.mmb"), str(FIX / "hol.mm0")], None),
        ("test_boundary", [str(bindir / "test_boundary")], None),
        ("test_dummy_identity", [sys.executable, "-I", str(TESTS / "test_dummy_identity.py"), check], None),
        ("test_mmb", [sys.executable, "-I", str(TESTS / "test_mmb.py"), check], None),
    ]
    for name, cmd, stdin in tests:
        rc, _ = run(cmd, stdin=stdin, timeout=300)
        if rc != 0:
            return name + (" (timeout)" if rc == "timeout" else "")
    return None


def run_core(core, mutant):
    name, file, old, new = mutant
    mdir = core.work / name
    mdir.mkdir()
    mutate(name, file, old, new, mdir / file)
    bindir, out = core.build(mdir, file)
    if bindir is None:
        return name, "BUILD-FAILED", out.strip().splitlines()[-1:] if out else []
    killer = core_tests(bindir)
    shutil.rmtree(mdir, ignore_errors=True)
    return name, ("KILLED" if killer else "SURVIVED"), [killer] if killer else []


def run_host(mutants, work, make_flags):
    """Rebuild CeTTa in a copy of the tree for each mutant and run the host
    tests; the copy keeps the tree's objects, so only the mutated file and
    the link are rebuilt."""
    tree = work / "tree"
    print(f"copying the tree to {tree}", flush=True)
    shutil.copytree(ROOT, tree, symlinks=True)
    make = ["make"] + shlex.split(make_flags)
    rc, out = run(make, cwd=tree, timeout=3600)
    if rc != 0:
        raise SystemExit("host baseline build failed:\n" + out[-2000:])
    rc, out = run([sys.executable, "-I", str(tree / "tests/mm0/test_host.py"), str(tree / "cetta")],
                  cwd=tree, timeout=3600)
    if rc != 0:
        raise SystemExit("host baseline tests failed:\n" + out[-2000:])
    results = []
    for name, file, old, new in mutants:
        src = tree / "native/mm0" / file
        original = src.read_text()
        mutate(name, file, old, new, src)
        rc, out = run(make, cwd=tree, timeout=3600)
        if rc != 0:
            results.append((name, "BUILD-FAILED", out.strip().splitlines()[-1:]))
        else:
            rc, out = run([sys.executable, "-I", str(tree / "tests/mm0/test_host.py"), str(tree / "cetta")],
                          cwd=tree, timeout=3600)
            fails = [line[:200] for line in out.splitlines()
                     if line.startswith(("FAIL", "AssertionError"))][:2]
            results.append((name, "KILLED" if rc != 0 else "SURVIVED",
                            ["test_host" + (" (timeout)" if rc == "timeout" else "")] + fails if rc != 0 else []))
        src.write_text(original)
        os.utime(src)
        print(f"  host {results[-1][0]}: {results[-1][1]} {' '.join(map(str, results[-1][2]))}", flush=True)
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--host", action="store_true",
                    help="also run module mutants and surviving semantic mutants through test_host.py")
    ap.add_argument("--make-flags", default="-j4 BUILD=core ENABLE_LIB_PROLOG=0 ENABLE_HTTP=0 ENABLE_DURABLE=0 "
                    "CFLAGS='-O2 -Wall -std=c11 -pthread'")
    ap.add_argument("--keep", action="store_true", help="keep the work directory")
    args = ap.parse_args()
    mutants = [m for m in MUTANTS if not args.only or m[0] in args.only]
    work = Path(tempfile.mkdtemp(prefix="mm0-mutation-"))
    cc = shlex.split(os.environ.get("CC", "cc"))
    cflags = ["-O1", "-std=c11", "-w"]
    try:
        core = Core(work, cc, cflags)
        # The unmutated build must pass every test.
        base = work / "unmutated"
        base.mkdir()
        objs = [str(core.base / (Path(f).stem + ".o")) for f in CORE]
        for prog in ("mm0_check", "test_mm0", "test_boundary"):
            rc, out = run(cc + [str(core.base / (prog + ".o"))] + objs + ["-o", str(base / prog)])
            if rc != 0:
                raise SystemExit("baseline link failed:\n" + out)
        failing = core_tests(base)
        if failing:
            raise SystemExit(f"the unmutated build fails {failing}")
        core_mutants = [m for m in mutants if m[1] != MOD]
        with ThreadPoolExecutor(args.jobs) as pool:
            results = list(pool.map(lambda m: run_core(core, m), core_mutants))
        for name, verdict, detail in results:
            print(f"  core {name}: {verdict} {' '.join(map(str, detail))}", flush=True)
        if args.host:
            survivors = {r[0] for r in results if r[1] == "SURVIVED"}
            host = [m for m in mutants if m[1] == MOD or (m[1] == SEM and m[0] in survivors)]
            host_results = {r[0]: r for r in run_host(host, work, args.make_flags)}
            results = [host_results.get(r[0], r) for r in results] + \
                      [r for name, r in host_results.items() if name not in {x[0] for x in results}]
        killed = sum(1 for r in results if r[1] == "KILLED")
        equivalent = [r[0] for r in results if r[1] == "SURVIVED" and r[0] in EQUIVALENT]
        survived = [r[0] for r in results if r[1] == "SURVIVED" and r[0] not in EQUIVALENT]
        broken = [r[0] for r in results if r[1] == "BUILD-FAILED"]
        print(f"Mm0MutationSummary {len(results)} killed {killed} equivalent {len(equivalent)} "
              f"survived {len(survived)} build-failed {len(broken)}")
        for name in equivalent:
            print(f"  equivalent {name}: {EQUIVALENT[name]}")
        for name in survived:
            print(f"  survivor {name}")
        for name in broken:
            print(f"  build-failed {name}")
        return 1 if survived or broken else 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
