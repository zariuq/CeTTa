#ifdef NDEBUG
#undef NDEBUG
#endif
#include "eval.h"
#include "native/seal_module.h"
#include "runtime_names.h"
#include "runtime_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static bool refuse_snapshot;

Atom *__real_atom_expr(Arena *arena, Atom **elements, CettaExprLen count);

Atom *__wrap_atom_expr(Arena *arena, Atom **elements, CettaExprLen count) {
    if (refuse_snapshot && count && atom_is_symbol(elements[0], "seal:snapshot")) {
        refuse_snapshot = false;
        return NULL;
    }
    return __real_atom_expr(arena, elements, count);
}

static CettaCallOutcome call(Arena *arena, const char *name, Atom **args, uint32_t count) {
    CettaCallOutcome outcome;
    assert(cetta_seal_module_call(NULL, NULL, arena, atom_symbol(arena, name), args, count, &outcome));
    return outcome;
}

int main(void) {
    CettaNameDomain names;
    cetta_name_domain_init(&names);
    CettaNameContext previous = cetta_name_context_current();
    CettaNameContext context = cetta_name_domain_context(&names);
    cetta_name_context_install(&context);
    Arena root, arena;
    arena_init_detached(&root);
    arena_init_detached(&arena);
    CettaRuntimeSession *session = calloc(1, sizeof *session);
    assert(session && cetta_runtime_session_init(session, &root, CETTA_LANGUAGE_HE, NULL));
    assert(cetta_runtime_session_open_root(session, &root));

    Atom *name = atom_symbol(&arena, "snapshot-test");
    CettaCallOutcome service = call(&arena, "__cetta_lib_seal_new-service", (Atom *[]){name}, 1);
    CettaCallOutcome key = call(&arena, "__cetta_lib_seal_new-key", (Atom *[]){name}, 1);
    assert(service.kind == CETTA_CALL_VALUE && service.term &&
        service.term->kind == ATOM_EXPR && service.term->expr.len == 4);
    assert(key.kind == CETTA_CALL_VALUE && key.term);
    Atom **facets = service.term->expr.elems;
    Atom *space = eval_new_managed_space(&session->space, &arena);
    assert(space && space->kind == ATOM_GROUNDED && space->ground.gkind == GV_SPACE);
    Atom *row = atom_symbol(&arena, "entry");
    assert(space_admit_scoped_atom(space->ground.ptr, &arena, row));
    CettaCallOutcome box = call(&arena, "__cetta_lib_seal_lock",
        (Atom *[]){facets[3], key.term, space}, 3);
    assert(box.kind == CETTA_CALL_VALUE && box.term);
    CettaCallOutcome grant = call(&arena, "__cetta_lib_seal_grant",
        (Atom *[]){facets[2], key.term, atom_symbol(&arena, "once")}, 3);
    assert(grant.kind == CETTA_CALL_VALUE && grant.term);

    Atom *request[] = {facets[1], box.term, key.term};
    refuse_snapshot = true;
    CettaCallOutcome failure = call(&arena, "__cetta_lib_seal_read-space", request, 3);
    assert(!refuse_snapshot && failure.kind == CETTA_CALL_RAISED && failure.term);
    CettaCallOutcome retry = call(&arena, "__cetta_lib_seal_read-space", request, 3);
    assert(retry.kind == CETTA_CALL_VALUE && retry.term && retry.term->kind == ATOM_EXPR);
    assert(retry.term->expr.len == 2 && atom_is_symbol(retry.term->expr.elems[0], "seal:snapshot"));
    assert(atom_eq(retry.term->expr.elems[1], row));
    assert(call(&arena, "__cetta_lib_seal_read-space", request, 3).kind == CETTA_CALL_RAISED);

    arena_free(&arena);
    cetta_runtime_session_dispose(session);
    free(session);
    arena_free(&root);
    cetta_name_context_install(&previous);
    cetta_name_domain_free(&names);
    puts("PASS: snapshot preparation failure preserves a one-use seal grant");
    return 0;
}
