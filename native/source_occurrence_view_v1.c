#include "source_occurrence_view_v1.h"
#include "src/library.h"
#include "src/native_handle.h"
#include "src/petta_program.h"
#include "src/petta_specializer.h"
#include "src/shared_transition.h"
#include "src/space.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A captured occurrence view keeps one input Space's logical occurrences
 * as they were at capture. A selection lends structural records of the
 * occurrences whose head names were asked for, in capture order, and
 * retains their capture coordinates. A contraction removes exactly the
 * selected occurrences it names from the same, unchanged Space. */

#define KIND "cetta.source-occurrence-view.v1"

typedef struct {
    Arena storage;
    SpaceReadToken token;
    bool overlay;
    bool pinned;
    SpaceHeadSnapshot snapshot;
    Atom **rows;           /* copied mode: every logical occurrence */
    CettaIndex *positions; /* copied mode: their logical coordinates */
    CettaCount row_len;
    CettaIndex *selected;  /* coordinates of the last selection */
    CettaCount selected_len;
    bool has_selection;
    bool contracted;
} OccurrenceView;

static void release(void *raw) {
    OccurrenceView *view = raw;
    if (view->pinned) space_head_snapshot_release(&view->snapshot);
    free(view->rows);
    free(view->positions);
    free(view->selected);
    arena_free(&view->storage);
    free(view);
}

static Atom *error(Arena *a, Atom *head, const char *message) {
    return atom_error(a, head, atom_string(a, message));
}

static Atom *form1(Arena *a, const char *head, Atom *value) {
    Atom *parts[] = {atom_symbol(a, head), value};
    return atom_expr(a, parts, 2u);
}

typedef struct {
    char *bytes;
    size_t length, capacity;
    bool failed;
} Text;

static void text_put(Text *text, const char *bytes, size_t length) {
    if (text->failed) return;
    if (length > SIZE_MAX - text->length - 1u) { text->failed = true; return; }
    if (text->length + length + 1u > text->capacity) {
        size_t next = text->capacity ? text->capacity : 256u;
        while (next < text->length + length + 1u) {
            if (next > SIZE_MAX / 2u) { text->failed = true; return; }
            next *= 2u;
        }
        char *grown = realloc(text->bytes, next);
        if (!grown) { text->failed = true; return; }
        text->bytes = grown;
        text->capacity = next;
    }
    memcpy(text->bytes + text->length, bytes, length);
    text->length += length;
    text->bytes[text->length] = '\0';
}

static void text_cstr(Text *text, const char *bytes) { text_put(text, bytes, strlen(bytes)); }

/* JSON string body: quotes, backslashes and control characters escaped;
 * other bytes, including UTF-8 sequences, are kept. */
static void text_json_string(Text *text, const char *bytes, size_t length) {
    text_cstr(text, "\"");
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == '"') text_cstr(text, "\\\"");
        else if (c == '\\') text_cstr(text, "\\\\");
        else if (c < 0x20u) {
            char escape[8];
            snprintf(escape, sizeof escape, "\\u%04x", (unsigned)c);
            text_cstr(text, escape);
        } else text_put(text, (const char *)&bytes[i], 1u);
    }
    text_cstr(text, "\"");
}

static void text_field(Text *text, const char *key, const char *bytes, size_t length) {
    text_cstr(text, "{\"");
    text_cstr(text, key);
    text_cstr(text, "\":");
    text_json_string(text, bytes, length);
    text_cstr(text, "}");
}

static const char *grounded_kind(GroundedKind kind) {
    switch (kind) {
    case GV_SPACE: return "space";
    case GV_STATE: return "state";
    case GV_RATIONAL: return "rational";
    default: return "grounded";
    }
}

/* Structural record of a host atom. The kind of every leaf is kept, so a
 * consumer can refuse what its language does not denote. */
static void encode_atom(Text *text, Atom *atom, unsigned depth) {
    if (!atom || depth > 4096u) { text->failed = true; return; }
    switch (atom->kind) {
    case ATOM_SYMBOL: {
        const uint8_t *name = (const uint8_t *)symbol_bytes(g_symbols, atom->sym_id);
        if (!name) { text->failed = true; return; }
        text_field(text, "symbol", (const char *)name, symbol_len(g_symbols, atom->sym_id));
        return;
    }
    case ATOM_VAR: {
        const char *name = atom_name_cstr(atom);
        text_field(text, "variable", name ? name : "", name ? strlen(name) : 0u);
        return;
    }
    case ATOM_EXPR:
        text_cstr(text, "{\"expression\":[");
        for (CettaExprLen i = 0; i < atom->expr.len; i++) {
            if (i) text_cstr(text, ",");
            encode_atom(text, atom->expr.elems[i], depth + 1u);
        }
        text_cstr(text, "]}");
        return;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT: {
            char digits[32];
            snprintf(digits, sizeof digits, "%" PRId64, atom->ground.ival);
            text_field(text, "integer", digits, strlen(digits));
            return;
        }
        case GV_BIGINT: {
            const char *digits = atom_bigint_cstr(atom);
            if (!digits) { text->failed = true; return; }
            text_field(text, "integer", digits, strlen(digits));
            return;
        }
        case GV_FLOAT: {
            char digits[64];
            snprintf(digits, sizeof digits, "%.17g", atom->ground.fval);
            text_field(text, "float", digits, strlen(digits));
            return;
        }
        case GV_BOOL:
            text_cstr(text, atom->ground.bval ? "{\"bool\":true}" : "{\"bool\":false}");
            return;
        case GV_STRING:
            text_field(text, "string", atom->ground.sval, atom_string_len(atom));
            return;
        default: {
            const char *kind = grounded_kind(atom->ground.gkind);
            text_field(text, "grounded", kind, strlen(kind));
            return;
        }
        }
    default:
        text_field(text, "grounded", "atom", 4u);
        return;
    }
}

static SymbolId row_head(Atom *row) {
    if (row && row->kind == ATOM_SYMBOL) return row->sym_id;
    if (row && row->kind == ATOM_EXPR && row->expr.len && row->expr.elems[0]->kind == ATOM_SYMBOL)
        return row->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

static bool selected_head(SymbolId head, const SymbolId *heads, size_t count) {
    for (size_t i = 0; i < count; i++) if (heads[i] == head) return true;
    return false;
}

typedef struct {
    const char *text;
    size_t len;
} StringHead;

static bool row_string_head(Atom *row, const char **text, size_t *len) {
    Atom *head = NULL;
    if (row && row->kind == ATOM_GROUNDED && row->ground.gkind == GV_STRING)
        head = row;
    else if (row && row->kind == ATOM_EXPR && row->expr.len &&
             row->expr.elems[0]->kind == ATOM_GROUNDED &&
             row->expr.elems[0]->ground.gkind == GV_STRING)
        head = row->expr.elems[0];
    if (!head) return false;
    *text = head->ground.sval;
    *len = atom_string_len(head);
    return true;
}

static bool selected_row(Atom *row, const SymbolId *heads, size_t count,
                         const StringHead *strings, size_t string_count) {
    if (selected_head(row_head(row), heads, count)) return true;
    const char *text = NULL;
    size_t len = 0u;
    if (string_count && row_string_head(row, &text, &len))
        for (size_t i = 0; i < string_count; i++)
            if (strings[i].len == len && memcmp(strings[i].text, text, len) == 0) return true;
    return false;
}

static bool append_coordinate(CettaIndex **items, CettaCount *length, CettaCount *capacity,
                              CettaIndex value) {
    if (*length == *capacity) {
        CettaCount next = *capacity ? *capacity * 2u : 16u;
        if (next < *capacity || next > SIZE_MAX / sizeof(**items)) return false;
        CettaIndex *grown = realloc(*items, (size_t)next * sizeof(**items));
        if (!grown) return false;
        *items = grown;
        *capacity = next;
    }
    (*items)[(*length)++] = value;
    return true;
}

static Atom *capture(CettaLibraryContext *ctx, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 1u || !args[0] || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_SPACE)
        return error(a, head, "occurrence capture expects a Space");
    Space *space = args[0]->ground.ptr;
    OccurrenceView *view = calloc(1, sizeof(*view));
    if (!view) return error(a, head, "occurrence view allocation failed");
    arena_init(&view->storage);
    view->token = space_read_token(space);
    view->overlay = space->overlay_base != NULL;
    if (space_head_snapshot_init(space, &view->snapshot)) {
        view->pinned = true;
        view->token = view->snapshot.rows.read;
    } else {
        /* Layouts without a head index keep a copy of every logical
         * occurrence with its coordinate, taken in one shared transition. */
        view->token = space_read_token(space);
        CettaCount length = space_length64(space);
        if (length > SIZE_MAX / sizeof(Atom *)) { release(view); return error(a, head, "input too large"); }
        view->rows = length ? malloc((size_t)length * sizeof(Atom *)) : NULL;
        view->positions = length ? malloc((size_t)length * sizeof(CettaIndex)) : NULL;
        if (length && (!view->rows || !view->positions)) {
            release(view); return error(a, head, "occurrence copy allocation failed");
        }
        for (CettaIndex i = 0; i < length; i++) {
            Atom *row = space_get_at64(space, i);
            Atom *copy = row ? atom_deep_copy(&view->storage, row) : NULL;
            if (!copy) { release(view); return error(a, head, "occurrence copy failed"); }
            view->rows[i] = copy;
            view->positions[i] = i;
        }
        view->row_len = length;
    }
    uint64_t id;
    if (!cetta_native_handle_alloc(ctx, KIND, view, release, &id)) {
        release(view); return error(a, head, "occurrence view publication failed");
    }
    Atom *owner = cetta_native_handle_owned_atom(ctx, a, KIND, id);
    if (!owner) { cetta_native_handle_close(ctx, KIND, id); return error(a, head, "occurrence owner allocation failed"); }
    return form1(a, "SourceOccurrenceViewV1", owner);
}

static Atom *select_rows(CettaLibraryContext *ctx, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    OccurrenceView *view = nargs == 2u ? cetta_native_handle_get_owned(ctx, args[0], KIND) : NULL;
    Atom *names = nargs == 2u ? args[1] : NULL;
    if (!view || !names || names->kind != ATOM_EXPR)
        return error(a, head, "expected an occurrence view and a tuple of head names");
    if (view->has_selection) return error(a, head, "an occurrence view is selected once");
    size_t requested = names->expr.len;
    SymbolId *heads = requested ? malloc(requested * sizeof(*heads)) : NULL;
    StringHead *strings = requested ? malloc(requested * sizeof(*strings)) : NULL;
    if (requested && (!heads || !strings)) {
        free(heads); free(strings); return error(a, head, "head selection allocation failed");
    }
    size_t count = 0u, string_count = 0u;
    for (size_t i = 0; i < requested; i++) {
        Atom *name = names->expr.elems[i];
        if (!name || name->kind != ATOM_GROUNDED || name->ground.gkind != GV_STRING) {
            free(heads); free(strings); return error(a, head, "input heads must be strings");
        }
        size_t len = atom_string_len(name);
        SymbolId symbol = symbol_intern_bytes(g_symbols, (const uint8_t *)name->ground.sval, len);
        if (symbol != SYMBOL_ID_NONE) heads[count++] = symbol;
        strings[string_count++] = (StringHead){name->ground.sval, len};
    }
    Text text = {0};
    text_cstr(&text, "[");
    CettaIndex *selected = NULL;
    CettaCount length = 0u, capacity = 0u;
    bool complete = true;
    if (view->pinned) {
        /* String heads are not in the head index: those selections read
         * every pinned row in logical order. */
        SpaceOccurrenceCursor cursor;
        bool ready = string_count
            ? space_occurrence_cursor_clone(&view->snapshot.rows, &cursor)
            : space_head_snapshot_select(&view->snapshot, heads, count, &cursor);
        if (!ready) {
            free(heads); free(strings); free(text.bytes);
            return error(a, head, "captured occurrence selection failed");
        }
        SpaceOccurrenceCursorStep step;
        CettaIndex logical = 0u;
        Atom *row = NULL;
        while ((step = space_occurrence_cursor_next(&cursor, &logical, &row)) == SPACE_OCCURRENCE_CURSOR_ITEM) {
            if (!selected_row(row, heads, count, strings, string_count)) continue;
            if (length) text_cstr(&text, ",");
            encode_atom(&text, row, 0u);
            if (!append_coordinate(&selected, &length, &capacity, logical)) { complete = false; break; }
        }
        if (step != SPACE_OCCURRENCE_CURSOR_END) complete = false;
        space_occurrence_cursor_release(&cursor);
    } else {
        for (CettaCount i = 0; i < view->row_len; i++) {
            Atom *row = view->rows[i];
            if (!selected_row(row, heads, count, strings, string_count)) continue;
            if (length) text_cstr(&text, ",");
            encode_atom(&text, row, 0u);
            if (!append_coordinate(&selected, &length, &capacity, view->positions[i])) { complete = false; break; }
        }
    }
    free(heads);
    free(strings);
    text_cstr(&text, "]");
    if (!complete || text.failed) {
        free(selected); free(text.bytes);
        return error(a, head, "captured occurrence enumeration did not complete");
    }
    view->selected = selected;
    view->selected_len = length;
    view->has_selection = true;
    Atom *json = atom_string(a, text.bytes);
    free(text.bytes);
    return form1(a, "SourceOccurrencesV1", json);
}

static int compare_index(const void *left, const void *right) {
    CettaIndex a = *(const CettaIndex *)left, b = *(const CettaIndex *)right;
    return a < b ? -1 : a > b;
}

static Atom *refusal(Arena *a, const char *reason) {
    return form1(a, "SourceOccurrenceRefusedV1", atom_symbol(a, reason));
}

/* Remove exactly the named selected occurrences from the captured Space.
 * The Space must be the captured one, unchanged since capture. Another
 * Space reports SourceOccurrenceDistinctV1 and is not touched. */
static Atom *contract(CettaLibraryContext *ctx, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    OccurrenceView *view = nargs == 3u ? cetta_native_handle_get_owned(ctx, args[0], KIND) : NULL;
    if (!view || !args[1] || args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_SPACE ||
        !args[2] || args[2]->kind != ATOM_EXPR)
        return error(a, head, "expected an occurrence view, a Space and a tuple of ordinals");
    if (!view->has_selection) return error(a, head, "occurrence view has no selection");
    Space *live = args[1]->ground.ptr;
    if (view->token.space != live || view->token.instance_id != space_instance_id(live))
        return atom_symbol(a, "SourceOccurrenceDistinctV1");
    Atom *ordinals = args[2];
    CettaCount count = ordinals->expr.len;
    if (count == 0u) return form1(a, "SourceOccurrencesContractedV1", atom_int(a, 0));
    if (view->contracted) return refusal(a, "AlreadyContracted");
    if (view->overlay) return refusal(a, "OverlayCoordinates");
    if (!space_read_token_matches_live_space(view->token, live)) return refusal(a, "StaleCapture");
    CettaIndex *positions = malloc((size_t)count * sizeof(*positions));
    if (!positions) return error(a, head, "contraction allocation failed");
    for (CettaCount i = 0; i < count; i++) {
        Atom *ordinal = ordinals->expr.elems[i];
        if (!ordinal || ordinal->kind != ATOM_GROUNDED || ordinal->ground.gkind != GV_INT ||
            ordinal->ground.ival < 0 || (CettaCount)ordinal->ground.ival >= view->selected_len) {
            free(positions); return error(a, head, "contraction ordinal outside the selection");
        }
        positions[i] = view->selected[ordinal->ground.ival];
    }
    qsort(positions, (size_t)count, sizeof(*positions), compare_index);
    for (CettaCount i = 1; i < count; i++)
        if (positions[i] == positions[i - 1u]) {
            free(positions); return error(a, head, "contraction names one occurrence twice");
        }
    CettaCount logical_len = space_length64(live);
    if (positions[count - 1u] >= logical_len) {
        free(positions); return refusal(a, "StaleCapture");
    }
    Arena removed_storage;
    arena_init(&removed_storage);
    Atom **removed = malloc((size_t)count * sizeof(*removed));
    if (!removed) { free(positions); arena_free(&removed_storage); return error(a, head, "contraction allocation failed"); }
    for (CettaCount i = 0; i < count; i++) {
        Atom *row = space_get_at64(live, positions[i]);
        removed[i] = row ? atom_deep_copy(&removed_storage, row) : NULL;
        if (!removed[i]) {
            free(positions); free(removed); arena_free(&removed_storage);
            return error(a, head, "contraction could not read a captured occurrence");
        }
    }
    bool ok = space_remove_logical_positions(live, positions, count);
    if (!ok) {
        uint8_t *mask = logical_len ? calloc((size_t)logical_len, 1u) : NULL;
        if (!mask) { free(positions); free(removed); arena_free(&removed_storage); return error(a, head, "contraction allocation failed"); }
        for (CettaCount i = 0; i < count; i++) mask[positions[i]] = 1u;
        CettaCount contracted = 0u;
        ok = space_remove_occurrence_mask_stable(live, mask, logical_len, &contracted) && contracted == count;
        free(mask);
    }
    if (ok) {
        PettaProgram *program = ctx ? ctx->petta_program : NULL;
        for (CettaCount i = 0; i < count; i++) {
            petta_specializer_note_mutation(live, removed[i]);
            if (program) petta_program_note_remove_one(program, live, removed[i]);
        }
        view->contracted = true;
    }
    free(positions); free(removed); arena_free(&removed_storage);
    if (!ok) return refusal(a, "ContractionRefused");
    return form1(a, "SourceOccurrencesContractedV1", atom_int(a, (int64_t)count));
}

Atom *cetta_source_occurrence_view_dispatch_v1(CettaLibraryContext *ctx, Arena *a,
    Atom *head, Atom **args, uint32_t nargs) {
    bool captures = atom_is_symbol(head, "__cetta_lib_source_occurrence_capture_v1");
    bool selects = atom_is_symbol(head, "__cetta_lib_source_occurrence_select_v1");
    bool contracts = atom_is_symbol(head, "__cetta_lib_source_occurrence_contract_v1");
    if (!captures && !selects && !contracts) return NULL;
    /* Validation and the update it authorizes share one transition. A
     * mutation while this operation waits must invalidate its capture. */
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    if (captures) return capture(ctx, a, head, args, nargs);
    if (selects) return select_rows(ctx, a, head, args, nargs);
    return contract(ctx, a, head, args, nargs);
}
