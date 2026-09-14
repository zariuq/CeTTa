#ifndef CETTA_GSLT_TERM_VIEW_V1_H
#define CETTA_GSLT_TERM_VIEW_V1_H

#include "atom.h"

/* A borrowed term view is an immutable source term paired with the authority
 * that resolves its variable roots.  The source and every value returned by
 * the resolver must outlive the consuming call, and resolution must be stable
 * for that call.  An unbound variable may be returned unchanged; each
 * consumer decides whether its observation admits open variables. */
typedef enum {
    CETTA_GSLT_TERM_VIEW_OK_V1 = 0,
    CETTA_GSLT_TERM_VIEW_DEFER_V1,
    CETTA_GSLT_TERM_VIEW_INVALID_V1,
    CETTA_GSLT_TERM_VIEW_RESOURCE_V1,
} CettaGsltTermViewStatusV1;

typedef CettaGsltTermViewStatusV1 (*CettaGsltTermViewResolveV1)(
    void *context, Atom *source_variable, Atom **target_out);

typedef struct {
    Atom *source;
    CettaGsltTermViewResolveV1 resolve;
    void *resolve_context;
} CettaGsltTermViewV1;

/* A recursive observation retains the environment of each subtree. Following
 * a variable may enter a different environment; its children must inherit
 * that resulting environment rather than the variable's original one.
 * `scope` is opaque to structural consumers and owned by the resolver.
 * All sources, scopes and returned atoms are borrowed for one stable read. */
typedef struct {
    Atom *source;
    const void *scope;
} CettaGsltTermCursorV1;

typedef CettaGsltTermViewStatusV1 (*CettaGsltTermCursorResolveV1)(
    void *context, CettaGsltTermCursorV1 source,
    CettaGsltTermCursorV1 *target_out);

typedef struct {
    CettaGsltTermCursorResolveV1 resolve;
    void *context;
} CettaGsltTermCursorObserverV1;

static inline CettaGsltTermViewStatusV1
cetta_gslt_term_cursor_resolve_root_v1(
        CettaGsltTermCursorObserverV1 observer,
        CettaGsltTermCursorV1 source,
        CettaGsltTermCursorV1 *target_out) {
    if (!target_out)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    *target_out = (CettaGsltTermCursorV1){0};
    if (!source.source)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    if (source.source->kind != ATOM_VAR || !observer.resolve) {
        if (source.source->kind == ATOM_VAR && source.scope && !observer.resolve)
            return CETTA_GSLT_TERM_VIEW_DEFER_V1;
        *target_out = source;
        return CETTA_GSLT_TERM_VIEW_OK_V1;
    }
    CettaGsltTermViewStatusV1 status = observer.resolve(
        observer.context, source, target_out);
    if (status < CETTA_GSLT_TERM_VIEW_OK_V1 ||
        status > CETTA_GSLT_TERM_VIEW_RESOURCE_V1 ||
        (status == CETTA_GSLT_TERM_VIEW_OK_V1 && !target_out->source)) {
        *target_out = (CettaGsltTermCursorV1){0};
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    }
    return status;
}

/* The parent must already have been root-resolved. This operation changes
 * only the term coordinate, preserving the environment selected by that
 * resolution. A missing child is distinct from an unresolved parent. */
static inline bool cetta_gslt_term_cursor_child_v1(
        CettaGsltTermCursorV1 parent, CettaExprIndex index,
        CettaGsltTermCursorV1 *child_out) {
    if (!child_out)
        return false;
    *child_out = (CettaGsltTermCursorV1){0};
    if (!parent.source || parent.source->kind != ATOM_EXPR ||
        index >= parent.source->expr.len)
        return false;
    *child_out = (CettaGsltTermCursorV1){
        .source = parent.source->expr.elems[index], .scope = parent.scope,
    };
    return true;
}

/* Whether a consumer retains bindings for variable roots which remain open
 * after resolution.  A backend may erase such roots only under DEAD, and it
 * must still enforce any structural premise required by its operation. */
typedef enum {
    CETTA_GSLT_TERM_VIEW_OPEN_BINDINGS_OBSERVED_V1 = 0,
    CETTA_GSLT_TERM_VIEW_OPEN_BINDINGS_DEAD_V1,
} CettaGsltTermViewOpenBindingObservationV1;

static inline bool cetta_gslt_term_view_open_binding_observation_valid_v1(
        CettaGsltTermViewOpenBindingObservationV1 observation) {
    return observation >=
               CETTA_GSLT_TERM_VIEW_OPEN_BINDINGS_OBSERVED_V1 &&
        observation <=
               CETTA_GSLT_TERM_VIEW_OPEN_BINDINGS_DEAD_V1;
}

static inline CettaGsltTermViewStatusV1
cetta_gslt_term_view_resolve_root_v1(
    const CettaGsltTermViewV1 *view, Atom *source, Atom **target_out) {
    if (target_out)
        *target_out = NULL;
    if (!view || !view->source || !source || !target_out)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    if (source->kind != ATOM_VAR) {
        *target_out = source;
        return CETTA_GSLT_TERM_VIEW_OK_V1;
    }
    if (!view->resolve)
        return CETTA_GSLT_TERM_VIEW_DEFER_V1;
    CettaGsltTermViewStatusV1 status = view->resolve(
        view->resolve_context, source, target_out);
    if (status == CETTA_GSLT_TERM_VIEW_OK_V1 && !*target_out)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    if (status < CETTA_GSLT_TERM_VIEW_OK_V1 ||
        status > CETTA_GSLT_TERM_VIEW_RESOURCE_V1) {
        *target_out = NULL;
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    }
    return status;
}

/* Project the immediate coordinates of an expression view without building
 * a replacement expression.  Nested resolved values remain borrowed atoms;
 * a consumer which observes open bindings must reject them, while a consumer
 * which proves them dead may retain distinct variables as wildcards. */
static inline bool cetta_gslt_term_view_project_expression_coordinates_v1(
        const CettaGsltTermViewV1 *view,
        CettaGsltTermViewOpenBindingObservationV1 open_bindings,
        Atom **coordinates, CettaExprLen coordinate_count) {
    if (!view || !view->source || !coordinates ||
        !cetta_gslt_term_view_open_binding_observation_valid_v1(
            open_bindings) ||
        view->source->kind != ATOM_EXPR ||
        view->source->expr.len == 0u ||
        view->source->expr.len != coordinate_count) {
        return false;
    }
    for (CettaExprIndex index = 0u; index < coordinate_count; index++) {
        Atom *resolved = NULL;
        CettaGsltTermViewStatusV1 status =
            cetta_gslt_term_view_resolve_root_v1(
                view, view->source->expr.elems[index], &resolved);
        if (status != CETTA_GSLT_TERM_VIEW_OK_V1 || !resolved ||
            (resolved->kind != ATOM_VAR && atom_has_vars(resolved))) {
            return false;
        }
        if (resolved->kind == ATOM_VAR) {
            if (open_bindings !=
                    CETTA_GSLT_TERM_VIEW_OPEN_BINDINGS_DEAD_V1) {
                return false;
            }
            for (CettaExprIndex previous = 0u;
                 previous < index; previous++) {
                if (coordinates[previous]->kind == ATOM_VAR &&
                    coordinates[previous]->var_id == resolved->var_id) {
                    return false;
                }
            }
        }
        coordinates[index] = resolved;
    }
    return true;
}

#endif /* CETTA_GSLT_TERM_VIEW_V1_H */
