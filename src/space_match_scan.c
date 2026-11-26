#include "space_match_scan.h"

#include "petta_semantics.h"

#include <stdlib.h>
#include <string.h>

/* Module order over a space with an imported prefix reads the space's own
 * rows, then its imported prefix, then the dependencies' rows. */
enum {
    SPACE_MATCH_SCAN_OWN = 0,
    SPACE_MATCH_SCAN_IMPORTED,
    SPACE_MATCH_SCAN_DEPENDENCIES,
    SPACE_MATCH_SCAN_DONE,
};

struct SpaceMatchScanModule {
    /* The imported prefix's rows are those below imported_len; a
     * dependency's rows are those from dependency_base. */
    CettaIndex imported_len;
    CettaIndex dependency_base;
    /* A second reader of the rows, opened with the first, which reads the
     * imported prefix after the own rows. */
    SpaceOccurrenceCursor imported;
    uint8_t phase;
    bool rows_done;
    /* The first dependency row, read before the imported prefix. */
    bool held;
    SpaceMatchScanRow held_row;
};

void space_match_scan_init_empty(SpaceMatchScan *scan) {
    memset(scan, 0, sizeof(*scan));
    space_occurrence_cursor_init_empty(&scan->rows);
}

static bool space_match_scan_engine_native(const Space *space) {
    return space && !space->overlay_base &&
           (space->match_backend.kind == SPACE_ENGINE_NATIVE ||
            space->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT);
}

bool space_match_scan_admits(const Space *space) {
    if (!space_match_scan_engine_native(space))
        return false;
    uint32_t dependencies = space_dependency_count(space);
    for (uint32_t d = 0u; d < dependencies; d++) {
        const Space *dependency = space_dependency_at(space, d);
        if (!space_match_scan_engine_native(dependency) ||
            dependency->native.universe != space->native.universe)
            return false;
    }
    return true;
}

bool space_match_scan_reads_as_engine(const Space *space) {
    return space_match_scan_admits(space) &&
        !space_has_dependencies(space) && space_imported_length(space) == 0u;
}

static void space_match_scan_module_free(SpaceMatchScanModule *module) {
    if (!module)
        return;
    space_occurrence_cursor_release(&module->imported);
    free(module);
}

static SpaceMatchScanModule *space_match_scan_module_new(void) {
    SpaceMatchScanModule *module = malloc(sizeof(*module));
    if (!module)
        return NULL;
    memset(module, 0, sizeof(*module));
    space_occurrence_cursor_init_empty(&module->imported);
    module->dependency_base = UINT64_MAX;
    return module;
}

SpaceMatchCursorStatus space_match_scan_open(
    SpaceMatchScan *scan, Space *space, Arena *arena, Atom *pattern,
    SpaceMatchScanOrder order, Atom **selector_out) {
    space_match_scan_init_empty(scan);
    Atom *selector = match_index_selector(arena, pattern, NULL);
    if (selector_out)
        *selector_out = selector;
    if (!selector)
        return SPACE_MATCH_CURSOR_FAILED;
    /* In module order the row-ordered cursor already reads the own rows,
     * then each dependency's; only an imported prefix, read after the own
     * rows, needs the module's phases. */
    bool module_order = order == SPACE_MATCH_SCAN_MODULE_ORDER;
    CettaIndex imported_len = module_order ? space_imported_length(space) : 0u;
    SpaceMatchScanModule *module = NULL;
    if (imported_len != 0u) {
        module = space_match_scan_module_new();
        if (!module)
            return SPACE_MATCH_CURSOR_FAILED;
    }
    bool opened = module_order
        ? space_occurrence_cursor_init_in_row_order(space, selector,
                                                    &scan->rows)
        : space_occurrence_cursor_init(space, selector, &scan->rows);
    if (opened && module) {
        module->imported_len = imported_len;
        module->dependency_base =
            space_occurrence_cursor_dependency_base(&scan->rows);
        /* The imported prefix is read after the own rows, at the point the
         * scan opened: its reader is opened now, on the same pin. */
        opened = space_occurrence_cursor_clone_unstarted(&scan->rows,
                                                         &module->imported);
    }
    if (!opened) {
        space_match_scan_module_free(module);
        space_occurrence_cursor_release(&scan->rows);
        space_match_scan_init_empty(scan);
        return SPACE_MATCH_CURSOR_DECLINED;
    }
    scan->module = module;
    scan->universe = space_occurrence_cursor_universe(&scan->rows);
    scan->open = true;
    return SPACE_MATCH_CURSOR_READY;
}

static bool space_match_scan_copy(const SpaceMatchScan *src,
                                  SpaceMatchScan *dst, bool unstarted) {
    space_match_scan_init_empty(dst);
    if (!src || !src->open)
        return false;
    bool rows = unstarted
        ? space_occurrence_cursor_clone_unstarted(&src->rows, &dst->rows)
        : space_occurrence_cursor_clone(&src->rows, &dst->rows);
    SpaceMatchScanModule *module = NULL;
    if (rows && src->module) {
        module = space_match_scan_module_new();
        rows = module != NULL;
    }
    if (rows && module) {
        const SpaceMatchScanModule *from = src->module;
        module->imported_len = from->imported_len;
        module->dependency_base = from->dependency_base;
        if (!unstarted) {
            module->phase = from->phase;
            module->rows_done = from->rows_done;
            module->held = from->held;
            module->held_row = from->held_row;
        }
        /* The imported reader is open until the prefix is read; a scan
         * from the start reads it again from its start. */
        if (unstarted || from->phase <= SPACE_MATCH_SCAN_IMPORTED)
            rows = unstarted
                ? space_occurrence_cursor_clone_unstarted(&from->imported,
                                                          &module->imported)
                : space_occurrence_cursor_clone(&from->imported,
                                                &module->imported);
    }
    if (!rows) {
        space_match_scan_module_free(module);
        space_occurrence_cursor_release(&dst->rows);
        space_match_scan_init_empty(dst);
        return false;
    }
    dst->module = module;
    dst->universe = src->universe;
    dst->open = true;
    return true;
}

bool space_match_scan_clone_unstarted(const SpaceMatchScan *src,
                                      SpaceMatchScan *dst) {
    return space_match_scan_copy(src, dst, true);
}

bool space_match_scan_clone(const SpaceMatchScan *src, SpaceMatchScan *dst) {
    return space_match_scan_copy(src, dst, false);
}

static SpaceMatchScanStep space_match_scan_cursor_next(
    SpaceOccurrenceCursor *cursor, SpaceMatchScanRow *row) {
    CettaIndex index = 0u;
    AtomId id = CETTA_ATOM_ID_NONE;
    SpaceOccurrenceCursorStep step =
        space_occurrence_cursor_next_id(cursor, &index, &id);
    if (step == SPACE_OCCURRENCE_CURSOR_ITEM) {
        row->id = id;
        row->index = index;
        return SPACE_MATCH_SCAN_ROW;
    }
    return step == SPACE_OCCURRENCE_CURSOR_END ? SPACE_MATCH_SCAN_END
                                               : SPACE_MATCH_SCAN_INVALIDATED;
}

/* Module order over the composed reader, whose rows come in row order: the
 * imported prefix (passed over here), the own rows, then the dependencies'
 * rows, the first of which waits while the imported prefix is read. */
static SpaceMatchScanStep space_match_scan_next_in_module_order(
    SpaceMatchScan *scan, SpaceMatchScanRow *row) {
    SpaceMatchScanModule *module = scan->module;
    for (;;) {
        switch (module->phase) {
        case SPACE_MATCH_SCAN_OWN: {
            SpaceMatchScanStep step =
                space_match_scan_cursor_next(&scan->rows, row);
            if (step == SPACE_MATCH_SCAN_INVALIDATED)
                return step;
            if (step == SPACE_MATCH_SCAN_END) {
                module->rows_done = true;
                module->phase = SPACE_MATCH_SCAN_IMPORTED;
                continue;
            }
            if (row->index < module->imported_len)
                continue;
            if (row->index >= module->dependency_base) {
                module->held = true;
                module->held_row = *row;
                module->phase = SPACE_MATCH_SCAN_IMPORTED;
                continue;
            }
            return SPACE_MATCH_SCAN_ROW;
        }
        case SPACE_MATCH_SCAN_IMPORTED: {
            SpaceMatchScanStep step =
                space_match_scan_cursor_next(&module->imported, row);
            if (step == SPACE_MATCH_SCAN_INVALIDATED)
                return step;
            if (step == SPACE_MATCH_SCAN_ROW &&
                row->index < module->imported_len)
                return step;
            /* The reader's later rows are own rows and dependency rows,
             * read already. */
            space_occurrence_cursor_release(&module->imported);
            module->phase = SPACE_MATCH_SCAN_DEPENDENCIES;
            continue;
        }
        case SPACE_MATCH_SCAN_DEPENDENCIES: {
            if (module->held) {
                module->held = false;
                *row = module->held_row;
                return SPACE_MATCH_SCAN_ROW;
            }
            if (!module->rows_done) {
                SpaceMatchScanStep step =
                    space_match_scan_cursor_next(&scan->rows, row);
                if (step != SPACE_MATCH_SCAN_END)
                    return step;
            }
            module->rows_done = true;
            module->phase = SPACE_MATCH_SCAN_DONE;
            return SPACE_MATCH_SCAN_END;
        }
        default:
            return SPACE_MATCH_SCAN_END;
        }
    }
}

SpaceMatchScanStep space_match_scan_next_row(SpaceMatchScan *scan,
                                             SpaceMatchScanRow *row) {
    if (!scan || !scan->open || !row)
        return SPACE_MATCH_SCAN_INVALIDATED;
    if (!scan->module)
        return space_match_scan_cursor_next(&scan->rows, row);
    return space_match_scan_next_in_module_order(scan, row);
}

SpaceMatchScanStep space_match_scan_next(
    SpaceMatchScan *scan, Arena *arena, SpaceMatchExtend extend,
    void *matcher, SpaceMatchScanRow *row_out) {
    for (;;) {
        SpaceMatchScanRow row;
        SpaceMatchScanStep step = space_match_scan_next_row(scan, &row);
        if (step != SPACE_MATCH_SCAN_ROW)
            return step;
        /* A row the pattern does not meet leaves nothing in the arena: the
         * scan holds one row's work at a time, not the rows it read. */
        ArenaMark mark = arena ? arena_mark(arena) : (ArenaMark){0};
        SpaceMatchRowVerdict verdict = extend(matcher, scan, row);
        if (verdict == SPACE_MATCH_ROW_MET) {
            if (row_out)
                *row_out = row;
            return SPACE_MATCH_SCAN_ROW;
        }
        if (verdict == SPACE_MATCH_ROW_FAILED)
            return SPACE_MATCH_SCAN_FAILED;
        if (arena)
            arena_reset(arena, mark);
    }
}

Atom *space_match_scan_row_view(const SpaceMatchScan *scan, Arena *arena,
                                SpaceMatchScanRow row) {
    if (!scan || row.id == CETTA_ATOM_ID_NONE)
        return NULL;
    /* The universe owns the view and its retention: a strict space keeps
     * nothing for a row it lends, another space keeps a row's decode from
     * its fourth read. */
    const Space *space = space_match_scan_space(scan);
    return space && space->strict
        ? term_universe_view_atom_rented(space_match_scan_universe(scan),
                                         arena, row.id)
        : term_universe_view_atom(space_match_scan_universe(scan), arena,
                                  row.id);
}

Atom *space_match_scan_row_atom(const SpaceMatchScan *scan, Arena *arena,
                                SpaceMatchScanRow row) {
    if (!scan || row.id == CETTA_ATOM_ID_NONE)
        return NULL;
    return term_universe_scoped_atom(space_match_scan_universe(scan), arena,
                                     row.id);
}

void space_match_scan_release(SpaceMatchScan *scan) {
    if (!scan)
        return;
    space_match_scan_module_free(scan->module);
    space_occurrence_cursor_release(&scan->rows);
    space_match_scan_init_empty(scan);
}
