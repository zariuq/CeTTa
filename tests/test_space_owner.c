#include "space.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    Space space;
    unsigned references;
    bool live;
    unsigned *destroyed;
    Arena data;
    Atom *row, *identity;
} Owner;
typedef struct { Owner *owner; unsigned position; } SourceCursor;
static Atom *parse(Arena *a, const char *text) {
    Atom **forms = NULL; assert(parse_metta_text(text, a, &forms) == 1);
    Atom *result = forms[0]; free(forms); return result;
}
static void retain(void *raw) { ++((Owner *)raw)->references; }
static void release(void *raw) {
    Owner *owner = raw; assert(owner->references);
    if (--owner->references) return;
    if (owner->live) space_free(&owner->space);
    ++*owner->destroyed; arena_free(&owner->data); free(owner);
}
static bool current(const void *raw) { return ((const Owner *)raw)->live; }
static const SpaceOwnerOps lifetime = {.retain = retain, .release = release, .current = current};
static bool source_open(Space *space, Atom *pattern, void **out, SpaceOccurrenceCursorStep *failure) {
    (void)pattern; (void)failure; SourceCursor *c = calloc(1, sizeof(*c));
    if (!c) return false;
    c->owner = space->lifetime.owner; *out = c; return true;
}
static bool source_clone(const void *raw, bool rewind, void **out) {
    SourceCursor *c = malloc(sizeof(*c)); if (!c) return false;
    *c = *(const SourceCursor *)raw; if (rewind) c->position = 0; *out = c; return true;
}
static bool source_select(const void *raw, Atom *pattern, void **out, SpaceOccurrenceCursorStep *failure) {
    (void)pattern; (void)failure; return source_clone(raw, true, out);
}
static SpaceOccurrenceCursorStep source_next(void *raw, CettaIndex *index) {
    SourceCursor *c = raw; if (c->position) return SPACE_OCCURRENCE_CURSOR_END;
    c->position = 1; *index = 0; return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *source_atom(const void *raw, CettaIndex index) {
    const SourceCursor *c = raw; return c->position && !index ? c->owner->row : NULL;
}
static Atom *source_identity(const void *raw, CettaIndex index) {
    const SourceCursor *c = raw; return c->position && !index ? c->owner->identity : NULL;
}
static uint64_t source_count(const Space *space) { (void)space; return 1; }
static const SpaceSourceCursorOps source_rows = {
    .clone = source_clone, .select = source_select, .next = source_next,
    .atom = source_atom, .identity = source_identity, .free = free,
};
static const SpaceMatchBackendOps source_ops = {
    .name = "owned-fixture", .occurrence_ops = &source_rows, .occurrence_open = source_open, .logical_len = source_count,
};
static Owner *make_owner(unsigned *destroyed, bool source) {
    Owner *owner = calloc(1, sizeof(*owner)); assert(owner);
    owner->references = 1; owner->live = true; owner->destroyed = destroyed;
    arena_init_detached(&owner->data); owner->row = parse(&owner->data, "(edge a b)");
    owner->identity = parse(&owner->data, "fixture-occurrence");
    space_init(&owner->space); owner->space.lifetime = (SpaceOwner){owner, &lifetime};
    if (source) { owner->space.match_backend.kind = SPACE_ENGINE_SOURCE; owner->space.match_backend.ops = &source_ops; }
    else assert(space_admit_atom(&owner->space, &owner->data, owner->row));
    return owner;
}
static size_t drain(SpaceOccurrenceCursor *cursor) {
    CettaIndex index; size_t count = 0; SpaceOccurrenceCursorStep step;
    while ((step = space_occurrence_cursor_next(cursor, &index, NULL)) == SPACE_OCCURRENCE_CURSOR_ITEM) ++count;
    assert(step == SPACE_OCCURRENCE_CURSOR_END); return count;
}
static void check(bool source) {
    unsigned destroyed = 0; Owner *owner = make_owner(&destroyed, source);
    Arena first, second, aliases; arena_init_detached(&first); arena_init_detached(&second); arena_init_detached(&aliases);
    Atom *value = atom_space(&first, &owner->space); assert(value);
    Atom *copy = atom_deep_copy(&second, value); assert(copy);
    release(owner); /* Only values retain the allocation now. */
    arena_free(&first); assert(!destroyed && resolve_space(NULL, copy) == &owner->space);
    SpaceReadView view, duplicate; assert(space_read_view_capture(&owner->space, &view));
    assert(space_read_view_clone(&view, &duplicate));
    SpaceOccurrenceCursor cursor, cloned; assert(space_occurrence_cursor_init_scan(&owner->space, &cursor));
    assert(space_occurrence_cursor_clone(&cursor, &cloned));
    Atom *alias = atom_deep_copy(&aliases, copy); assert(alias);
    arena_free(&second); assert(!destroyed && space_read_view_is_current(&view));
    assert(drain(&cursor) == 1 && drain(&cloned) == 1);
    space_occurrence_cursor_release(&cloned); space_occurrence_cursor_release(&cursor);
    if (source) {
        assert(space_occurrence_cursor_init_scan(&owner->space, &cursor));
        assert(space_occurrence_cursor_select(&cursor, owner->row, &cloned));
        assert(drain(&cloned) == 1); space_occurrence_cursor_release(&cloned); space_occurrence_cursor_release(&cursor);
    }
    owner->live = false; space_free(&owner->space);
    assert(!resolve_space(NULL, alias) && !space_read_view_is_current(&view));
    assert(!space_admit_atom(&owner->space, &owner->data, owner->row));
    SpaceReadView closed; assert(!space_read_view_capture(&owner->space, &closed));
    SpaceReadView stale; assert(space_read_view_clone(&view, &stale));
    assert(!space_read_view_is_current(&stale)); space_read_view_release(&stale);
    space_read_view_release(&view); space_read_view_release(&duplicate); assert(!destroyed);
    arena_free(&aliases); assert(destroyed == 1);
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    check(false); check(true); symbol_table_free(&symbols);
    puts("Space owners: arena copies, native/source cursors, captured views, closure and final release passed");
}
