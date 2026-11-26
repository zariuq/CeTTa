#ifndef CETTA_SPACE_MATCH_SCAN_H
#define CETTA_SPACE_MATCH_SCAN_H

#include "space.h"
#include "term_universe.h"

/*
 * One scan of a space's occurrences for a match pattern: the choice point a
 * match opens over the rows its pattern may meet (premise), each row tried in
 * turn by the dialect's matcher, which extends its bindings by the row or
 * rejects it (candidate), and the rest of the scan kept for the next try
 * (retry).
 *
 * The space's indexes select the rows: they may offer a row the pattern does
 * not meet and never omit one it meets, so the matcher decides, and the
 * scan's matches are those of a scan of every row.  The scan reads the space
 * as it stood when it opened: a row added since is not read, a row removed
 * since is still read, and a rewrite of the rows the scan cannot read past
 * ends it as invalidated.  A scan is confined to its reader.
 */

/* The order a scan reads the rows in. */
typedef enum {
    /* The order the indexes lend the rows in: a space answers a bag; a
     * stack's, a queue's or a priority space's rows in its kind's order. */
    SPACE_MATCH_SCAN_INDEX_ORDER = 0,
    /* The order a module space answers in: its own rows, then the rows it
     * imported (its imported prefix), then each dependency's own rows in
     * import order, each group in row order; a stack's, a queue's or a
     * priority space's rows in its kind's order.  Every occurrence read is
     * a row: a relation the pattern names gives its facts back first. */
    SPACE_MATCH_SCAN_MODULE_ORDER,
} SpaceMatchScanOrder;

typedef enum {
    SPACE_MATCH_SCAN_END = 0,
    SPACE_MATCH_SCAN_ROW,
    SPACE_MATCH_SCAN_INVALIDATED,
    /* The matcher could not try a row (SPACE_MATCH_ROW_FAILED). */
    SPACE_MATCH_SCAN_FAILED,
} SpaceMatchScanStep;

/* A row the scan read: its atom's id in the scan's universe and its index
 * among the occurrences of the space as the scan opened it. */
typedef struct {
    AtomId id;
    CettaIndex index;
} SpaceMatchScanRow;

/* The matcher's verdict on one row. */
typedef enum {
    /* The row does not meet the pattern: what the try built is dropped. */
    SPACE_MATCH_ROW_UNMET = 0,
    /* The matcher extended its bindings by the row. */
    SPACE_MATCH_ROW_MET,
    /* The matcher could not try the row (capacity). */
    SPACE_MATCH_ROW_FAILED,
} SpaceMatchRowVerdict;

/* Module order's further reading (space_match_scan.c): a second reader of
 * the same rows, opened with the first, which reads the imported prefix
 * after the own rows. */
typedef struct SpaceMatchScanModule SpaceMatchScanModule;

typedef struct SpaceMatchScan {
    /* The space's rows and, after them, each dependency's own rows. */
    SpaceOccurrenceCursor rows;
    /* Module order; NULL for index order. */
    SpaceMatchScanModule *module;
    /* The universe naming the rows the scan lends by id. */
    TermUniverse *universe;
    bool open;
} SpaceMatchScan;

/* The dialect's matcher: extend its bindings by ROW, building what it needs
 * in its own arena, which the scan returns to its mark on UNMET. */
typedef SpaceMatchRowVerdict (*SpaceMatchExtend)(
    void *matcher, const SpaceMatchScan *scan, SpaceMatchScanRow row);

void space_match_scan_init_empty(SpaceMatchScan *scan);
/* Open SCAN, empty, over SPACE's rows that PATTERN, resolved in its
 * bindings, may meet.  Only the indexes see the selector (the pattern read
 * as the index reads it, in ARENA); a decline releases what was opened and
 * leaves the selector to a reader that cannot read rows in place. */
SpaceMatchCursorStatus space_match_scan_open(
    SpaceMatchScan *scan, Space *space, Arena *arena, Atom *pattern,
    SpaceMatchScanOrder order, Atom **selector_out);
/* A new scan of the same rows from the start, sharing the opening's pin
 * and index frontier.  False when SRC cannot be read again from the start
 * (it has been read, or the space was written since it opened). */
bool space_match_scan_clone_unstarted(const SpaceMatchScan *src,
                                      SpaceMatchScan *dst);
/* A scan at SRC's exact place, owning its own readers. */
bool space_match_scan_clone(const SpaceMatchScan *src, SpaceMatchScan *dst);
/* The next row, matched by no one. */
SpaceMatchScanStep space_match_scan_next_row(SpaceMatchScan *scan,
                                             SpaceMatchScanRow *row);
/* The next row EXTEND meets.  ARENA, when not NULL, is the matcher's: each
 * row it does not meet leaves ARENA as the try found it. */
SpaceMatchScanStep space_match_scan_next(
    SpaceMatchScan *scan, Arena *arena, SpaceMatchExtend extend,
    void *matcher, SpaceMatchScanRow *row_out);
/* ROW as a view built in ARENA, which the owner of ARENA reclaims: a strict
 * space's rows are rented, so reading them keeps nothing for them. */
Atom *space_match_scan_row_view(const SpaceMatchScan *scan, Arena *arena,
                                SpaceMatchScanRow row);
/* ROW as the universe's atom, an opaque image retained in ARENA. */
Atom *space_match_scan_row_atom(const SpaceMatchScan *scan, Arena *arena,
                                SpaceMatchScanRow row);
static inline const TermUniverse *space_match_scan_universe(
    const SpaceMatchScan *scan) {
    return scan->universe;
}
static inline Space *space_match_scan_space(const SpaceMatchScan *scan) {
    return scan->rows.space;
}
static inline bool space_match_scan_is_open(const SpaceMatchScan *scan) {
    return scan->open;
}
/* Whether a match of SPACE may be read by a scan: a space of the native
 * engine, read in place (no overlay), whose dependencies are too.  A scan
 * may still decline when it opens. */
bool space_match_scan_admits(const Space *space);
/* Whether a scan in module order reads SPACE as the native engine's own
 * queries do: a space whose rows are all its own, with no imported prefix
 * and no dependencies, read in row order (a stack's, a queue's or a
 * priority space's in its kind's order). */
bool space_match_scan_reads_as_engine(const Space *space);
void space_match_scan_release(SpaceMatchScan *scan);

#endif
