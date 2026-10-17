#include "regex_text_v1.h"
#include "src/string_ops.h"
#include "unicode_class_v1.h"

#include <limits.h>
#include <stdlib.h>

typedef struct {
    size_t scalar, byte, original_scalar, original_byte, scalars, bytes;
} SourceRun;

typedef struct {
    Atom *original, *cleaned;
    CettaUtf8Unit *original_units, *cleaned_units;
    size_t original_len, cleaned_len;
    SourceRun *runs;
    size_t run_len;
} SourceMap;

static bool form(Atom *value, const char *name, size_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
        atom_is_symbol(value->expr.elems[0], name);
}

static bool string(Atom *value) {
    return value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING;
}

static Atom *failure(Arena *arena, const char *kind, const char *reason, size_t at) {
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, kind), atom_string(arena, reason),
        atom_int(arena, (int64_t)at), atom_int(arena, (int64_t)at)}, 4u);
}

static void *grow(void *data, size_t *capacity, size_t wanted, size_t width) {
    if (wanted <= *capacity) return data;
    size_t next = *capacity ? *capacity : 16u;
    while (next < wanted) {
        if (next > SIZE_MAX / 2u) return NULL;
        next *= 2u;
    }
    if (next > SIZE_MAX / width) return NULL;
    void *result = realloc(data, next * width);
    if (result) *capacity = next;
    return result;
}

static bool whitespace(UnicodeV1Ranges ranges, uint32_t scalar) {
    size_t low = 0u, high = ranges.len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (scalar < ranges.items[middle].low) high = middle;
        else if (scalar > ranges.items[middle].high) low = middle + 1u;
        else return true;
    }
    return false;
}

/* Recognize only a lexical flag directive. Every retained directive is still
 * passed to the authored grammar; this does not recognize a regex pattern. */
static size_t visible(const CettaUtf8Unit *units, size_t len, size_t at,
        bool extended, UnicodeV1Ranges spaces) {
    while (at < len && extended) {
        if (whitespace(spaces, units[at].scalar)) { ++at; continue; }
        if (units[at].scalar != '#') break;
        do { ++at; } while (at < len && units[at].scalar != '\n');
    }
    return at;
}
static bool flag_directive(const CettaUtf8Unit *units, size_t len, size_t at,
        bool current, UnicodeV1Ranges spaces, size_t *last, bool *scoped, bool *next) {
    size_t question = visible(units, len, at + 1u, current, spaces);
    if (question == len || units[question].scalar != '?') return false;
    bool enable = true, any = false, value = current;
    size_t i = visible(units, len, question + 1u, current, spaces);
    for (; i < len; i = visible(units, len, i + 1u, current, spaces)) {
        uint32_t cp = units[i].scalar;
        if (cp == '-') enable = false;
        else if (cp == 'x') value = enable;
        else if (cp != 'i' && cp != 'm' && cp != 's' && cp != 'u' && cp != 'U') break;
        any = true;
    }
    if (i == len || (units[i].scalar != ':' && (units[i].scalar != ')' || !any)))
        return false;
    *last = i; *scoped = units[i].scalar == ':'; *next = value;
    return true;
}

static bool retain(CettaByteBuffer *output, SourceRun **runs, size_t *len,
        size_t *capacity, size_t *scalar, const char *source,
        const CettaUtf8Unit *unit, size_t original_scalar) {
    if (!*len || (*runs)[*len - 1u].original_scalar + (*runs)[*len - 1u].scalars != original_scalar) {
        void *p = grow(*runs, capacity, *len + 1u, sizeof(**runs));
        if (!p) return false;
        *runs = p;
        (*runs)[(*len)++] = (SourceRun){*scalar, output->len, original_scalar, unit->offset, 0u, 0u};
    }
    if (!cetta_byte_buffer_append(output, source + unit->offset, unit->width)) return false;
    ++(*runs)[*len - 1u].scalars;
    (*runs)[*len - 1u].bytes += unit->width;
    ++*scalar;
    return true;
}

typedef struct { size_t low, high; } DepthRange;
typedef struct { DepthRange *items; size_t len, capacity; } DepthSet;

/* Exact interval union for lexical bracket-depth alternatives. This is not
 * pattern parsing: only escapes, bracket scope and the historical first '['
 * literal are observed. Adjacent depth intervals share one representation. */
static bool depth_add(DepthSet *set, size_t low, size_t high) {
    if (low > high) return true;
    size_t first = 0u;
    while (first < set->len && set->items[first].high + 1u < low) ++first;
    size_t after = first;
    while (after < set->len && set->items[after].low <= high + 1u) {
        if (set->items[after].low < low) low = set->items[after].low;
        if (set->items[after].high > high) high = set->items[after].high;
        ++after;
    }
    if (first == after) {
        void *p = grow(set->items, &set->capacity, set->len + 1u, sizeof(*set->items));
        if (!p) return false;
        set->items = p;
        memmove(set->items + first + 1u, set->items + first, (set->len - first) * sizeof(*set->items));
        ++set->len;
    } else {
        memmove(set->items + first + 1u, set->items + after, (set->len - after) * sizeof(*set->items));
        set->len -= after - first - 1u;
    }
    set->items[first] = (DepthRange){low, high}; return true;
}
static bool class_region(const CettaUtf8Unit *units, size_t count, size_t from, size_t *last) {
    DepthSet states[6] = {{0}}, next[6] = {{0}};
    bool escaped = false, found = false, ok = depth_add(&states[0], 1u, 1u);
    size_t selected = count ? count - 1u : from;
    for (size_t i = from + 1u; ok && i < count; ++i) {
        uint32_t cp = units[i].scalar;
        for (size_t phase = 0u; phase < 6u; ++phase) next[phase].len = 0u;
        for (size_t phase = 0u; ok && phase < 6u; ++phase) {
            for (size_t j = 0u; ok && j < states[phase].len; ++j) {
                DepthRange range = states[phase].items[j];
                if (escaped || cp == '\\') ok = depth_add(&next[2], range.low, range.high);
                else if (cp == '[') {
                    ok = depth_add(&next[0], range.low + 1u, range.high + 1u);
                    if (ok && phase < 2u) ok = depth_add(&next[2], range.low, range.high);
                } else if (cp == ']') {
                    if (range.low == 1u) { selected = i; found = true; }
                    if (range.high > 1u) ok = depth_add(&next[2], range.low > 1u ? range.low - 1u : 1u, range.high - 1u);
                } else {
                    size_t after = cp == '&' ? (phase == 3u ? 0u : 3u) :
                        cp == '-' ? (phase == 4u ? 0u : 4u) :
                        cp == '~' ? (phase == 5u ? 0u : 5u) : cp == '^' && phase == 0u ? 1u : 2u;
                    ok = depth_add(&next[after], range.low, range.high);
                }
            }
        }
        escaped = !escaped && cp == '\\';
        bool live = false;
        for (size_t phase = 0u; phase < 6u; ++phase) {
            DepthSet swap = states[phase]; states[phase] = next[phase]; next[phase] = swap;
            live |= states[phase].len != 0u;
        }
        if (!live) break;
    }
    for (size_t phase = 0u; phase < 6u; ++phase) { free(states[phase].items); free(next[phase].items); }
    if (ok) *last = found ? selected : count - 1u;
    return ok;
}

Atom *cetta_re_v1_prepare_text(Arena *arena, Atom *text) {
    if (!string(text)) return failure(arena, "re:source-fault", "expected regex source text", 0u);
    size_t bytes = atom_string_len(text), count = 0u, bad = 0u;
    if (!cetta_utf8_count(text->ground.sval, bytes, &count, &bad))
        return failure(arena, "re:invalid-utf8", "regex source is not strict UTF-8", bad);
    CettaUtf8Unit *units = NULL;
    if (bytes > INT64_MAX || count > SIZE_MAX / sizeof(*units) ||
        !cetta_utf8_units(text->ground.sval, bytes, &units, &count, &bad))
        return failure(arena, "re:allocation-failure", "regex lexical source storage", 0u);
    UnicodeV1Ranges spaces;
    if (!unicode_v1_lexical_class("RegexWhitespace", &spaces)) {
        free(units);
        return failure(arena, "re:interpretation-needed", "extended-mode whitespace provider", 0u);
    }
    CettaByteBuffer output; cetta_byte_buffer_init(&output);
    SourceRun *runs = NULL; size_t run_len = 0u, run_cap = 0u, scalar = 0u;
    bool *scopes = NULL; size_t scope_len = 0u, scope_cap = 0u;
    bool extended = false;
    for (size_t i = 0u; i < count; ++i) {
        uint32_t cp = units[i].scalar;
        if (cp == '\\') {
            if (!retain(&output, &runs, &run_len, &run_cap, &scalar, text->ground.sval, &units[i], i)) goto allocation;
            if (i + 1u < count) {
                ++i;
                if (!retain(&output, &runs, &run_len, &run_cap, &scalar, text->ground.sval, &units[i], i)) goto allocation;
            }
            continue;
        }
        {
            if (extended && whitespace(spaces, cp)) continue;
            if (extended && cp == '#') {
                while (i + 1u < count && units[i + 1u].scalar != '\n') ++i;
                continue;
            }
            if (cp == '[') {
                size_t last;
                if (!class_region(units, count, i, &last)) goto allocation;
                for (; i <= last; ++i)
                    if (!retain(&output, &runs, &run_len, &run_cap, &scalar, text->ground.sval, &units[i], i)) goto allocation;
                --i; continue;
            }
            else if (cp == '(') {
                size_t last; bool scoped, next;
                if (flag_directive(units, count, i, extended, spaces, &last, &scoped, &next)) {
                    if (scoped) {
                        void *p = grow(scopes, &scope_cap, scope_len + 1u, sizeof(*scopes));
                        if (!p) goto allocation;
                        scopes = p; scopes[scope_len++] = extended;
                    }
                    for (; i <= last; ++i) {
                        if (extended && whitespace(spaces, units[i].scalar)) continue;
                        if (extended && units[i].scalar == '#') {
                            while (i < last && units[i + 1u].scalar != '\n') ++i;
                            continue;
                        }
                        if (!retain(&output, &runs, &run_len, &run_cap, &scalar, text->ground.sval, &units[i], i)) goto allocation;
                    }
                    --i; extended = next; continue;
                }
                void *p = grow(scopes, &scope_cap, scope_len + 1u, sizeof(*scopes));
                if (!p) goto allocation;
                scopes = p; scopes[scope_len++] = extended;
            } else if (cp == ')' && scope_len) extended = scopes[--scope_len];
        }
        if (!retain(&output, &runs, &run_len, &run_cap, &scalar, text->ground.sval, &units[i], i)) goto allocation;
    }
    Atom *owned = atom_string_n(arena, text->ground.sval, bytes);
    Atom *cleaned = output.len == bytes ? owned : atom_string_n(arena, output.bytes, output.len);
    Atom *mapping;
    if (output.len == bytes) mapping = atom_expr2(arena, atom_symbol(arena, "re:source-identity-v1"), owned);
    else {
        Atom **values = run_len ? malloc(run_len * sizeof(*values)) : NULL;
        if (run_len && !values) goto allocation;
        for (size_t i = 0u; i < run_len; ++i) {
            SourceRun r = runs[i];
            values[i] = atom_expr(arena, (Atom *[]){atom_symbol(arena, "re:source-run-v1"),
                atom_int(arena, r.scalar), atom_int(arena, r.byte), atom_int(arena, r.original_scalar),
                atom_int(arena, r.original_byte), atom_int(arena, r.scalars), atom_int(arena, r.bytes)}, 7u);
        }
        Atom *list = atom_expr(arena, values, run_len); free(values);
        mapping = atom_expr(arena, (Atom *[]){atom_symbol(arena, "re:source-map-v1"), owned, cleaned,
            atom_int(arena, count), atom_int(arena, scalar), list}, 6u);
    }
    Atom *result = atom_expr3(arena, atom_symbol(arena, "re:source-text"), cleaned, mapping);
    free(units); free(scopes); free(runs); cetta_byte_buffer_free(&output);
    return result;
allocation:
    free(units); free(scopes); free(runs); cetta_byte_buffer_free(&output);
    return failure(arena, "re:allocation-failure", "regex lexical preparation storage", 0u);
}

static void map_free(SourceMap *map) {
    free(map->original_units); free(map->cleaned_units); free(map->runs);
    *map = (SourceMap){0};
}

static bool number(Atom *value, size_t *out) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT ||
        value->ground.ival < 0 || (uint64_t)value->ground.ival > SIZE_MAX) return false;
    *out = (size_t)value->ground.ival; return true;
}

static size_t byte_at(const CettaUtf8Unit *units, size_t count, size_t bytes, size_t scalar) {
    return scalar == count ? bytes : units[scalar].offset;
}

static bool map_read(Atom *mapping, SourceMap *map, bool *allocation) {
    if (!form(mapping, "re:source-map-v1", 5u)) return false;
    map->original = mapping->expr.elems[1]; map->cleaned = mapping->expr.elems[2];
    if (!string(map->original) || !string(map->cleaned) ||
        !number(mapping->expr.elems[3], &map->original_len) ||
        !number(mapping->expr.elems[4], &map->cleaned_len)) return false;
    size_t original_count, cleaned_count, bad = 0u;
    size_t original_bytes = atom_string_len(map->original), cleaned_bytes = atom_string_len(map->cleaned);
    if (!cetta_utf8_count(map->original->ground.sval, original_bytes, &original_count, &bad) ||
        !cetta_utf8_count(map->cleaned->ground.sval, cleaned_bytes, &cleaned_count, &bad) ||
        original_count != map->original_len || cleaned_count != map->cleaned_len ||
        original_count > SIZE_MAX / sizeof(*map->original_units) ||
        cleaned_count > SIZE_MAX / sizeof(*map->cleaned_units)) return false;
    if (!cetta_utf8_units(map->original->ground.sval, original_bytes, &map->original_units, &original_count, &bad) ||
        !cetta_utf8_units(map->cleaned->ground.sval, cleaned_bytes, &map->cleaned_units, &cleaned_count, &bad)) {
        *allocation = true; return false;
    }
    Atom *list = mapping->expr.elems[5];
    if (!list || list->kind != ATOM_EXPR || list->expr.len > SIZE_MAX / sizeof(*map->runs)) return false;
    map->run_len = list->expr.len;
    map->runs = map->run_len ? calloc(map->run_len, sizeof(*map->runs)) : NULL;
    if (map->run_len && !map->runs) { *allocation = true; return false; }
    size_t scalar = 0u, byte = 0u, original_end = 0u;
    for (size_t i = 0u; i < map->run_len; ++i) {
        Atom *row = list->expr.elems[i]; SourceRun *r = &map->runs[i];
        if (!form(row, "re:source-run-v1", 6u) ||
            !number(row->expr.elems[1], &r->scalar) || !number(row->expr.elems[2], &r->byte) ||
            !number(row->expr.elems[3], &r->original_scalar) || !number(row->expr.elems[4], &r->original_byte) ||
            !number(row->expr.elems[5], &r->scalars) || !number(row->expr.elems[6], &r->bytes) ||
            !r->scalars || r->scalar != scalar || r->byte != byte ||
            r->original_scalar < original_end || r->original_scalar > original_count ||
            r->scalars > original_count - r->original_scalar || r->scalars > cleaned_count - scalar ||
            r->original_byte != byte_at(map->original_units, original_count, original_bytes, r->original_scalar) ||
            r->bytes != byte_at(map->original_units, original_count, original_bytes, r->original_scalar + r->scalars) - r->original_byte ||
            r->bytes != byte_at(map->cleaned_units, cleaned_count, cleaned_bytes, scalar + r->scalars) - byte ||
            memcmp(map->original->ground.sval + r->original_byte, map->cleaned->ground.sval + byte, r->bytes)) return false;
        scalar += r->scalars; byte += r->bytes; original_end = r->original_scalar + r->scalars;
    }
    return scalar == cleaned_count && byte == cleaned_bytes;
}

static const SourceRun *run_at(const SourceMap *map, size_t scalar) {
    size_t low = 0u, high = map->run_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const SourceRun *r = &map->runs[middle];
        if (scalar < r->scalar) high = middle;
        else if (scalar >= r->scalar + r->scalars) low = middle + 1u;
        else return r;
    }
    return NULL;
}

static size_t locate_scalar(const SourceMap *map, size_t at, bool stop) {
    if (!stop && at == map->cleaned_len) return map->original_len;
    const SourceRun *r = run_at(map, stop ? at - 1u : at);
    return r->original_scalar + at - r->scalar;
}

/* Rebuild bottom-up. Atoms may be hash-consed, so even a copied completed
 * expression must not be edited. Memo cells retain DAG sharing and detect an
 * active back edge rather than interpreting a cyclic value as a finite CST. */
typedef struct { Atom *key, *value; } PointerCell;
typedef struct { PointerCell *slots; size_t capacity, count; } PointerMap;
static size_t pointer_hash(Atom *value) {
    uintptr_t x = (uintptr_t)value;
    x ^= x >> 17u; x *= (uintptr_t)0xed5ad4bbu; x ^= x >> 11u;
    return (size_t)x;
}
static PointerCell *pointer_find(PointerMap *map, Atom *value) {
    if (!map->capacity) return NULL;
    size_t at = pointer_hash(value) & (map->capacity - 1u);
    while (map->slots[at].key && map->slots[at].key != value)
        at = (at + 1u) & (map->capacity - 1u);
    return map->slots[at].key ? &map->slots[at] : NULL;
}
static bool pointer_add(PointerMap *map, Atom *value) {
    if (!map->capacity || map->count >= map->capacity / 2u) {
        size_t cap = map->capacity ? map->capacity * 2u : 64u;
        if (cap <= map->capacity || cap > SIZE_MAX / sizeof(*map->slots)) return false;
        PointerCell *slots = calloc(cap, sizeof(*slots)); if (!slots) return false;
        for (size_t i = 0u; i < map->capacity; ++i) if (map->slots[i].key) {
            size_t at = pointer_hash(map->slots[i].key) & (cap - 1u);
            while (slots[at].key) at = (at + 1u) & (cap - 1u);
            slots[at] = map->slots[i];
        }
        free(map->slots); map->slots = slots; map->capacity = cap;
    }
    size_t at = pointer_hash(value) & (map->capacity - 1u);
    while (map->slots[at].key) at = (at + 1u) & (map->capacity - 1u);
    map->slots[at].key = value; ++map->count; return true;
}

static bool locate_fields(Arena *arena, Atom *value, const SourceMap *map) {
    if (value->expr.len >= 4u && (atom_is_symbol(value->expr.elems[0], "CstRuleV1") ||
            atom_is_symbol(value->expr.elems[0], "SourceSpanNodeV1"))) {
        size_t start, stop;
        if (!number(value->expr.elems[2], &start) || !number(value->expr.elems[3], &stop) ||
                start > stop || stop > map->cleaned_len) return false;
        value->expr.elems[2] = atom_int(arena, locate_scalar(map, start, false));
        value->expr.elems[3] = atom_int(arena, locate_scalar(map, stop, start != stop));
    } else if (form(value, "ParserSpanV1", 4u)) {
        size_t first_byte, last_byte, first_scalar, last_scalar;
        if (!number(value->expr.elems[1], &first_byte) || !number(value->expr.elems[2], &last_byte) ||
            !number(value->expr.elems[3], &first_scalar) || !number(value->expr.elems[4], &last_scalar) ||
            first_byte || first_scalar || last_byte != atom_string_len(map->cleaned) ||
            last_scalar != map->cleaned_len) return false;
        value->expr.elems[1] = atom_int(arena, 0);
        value->expr.elems[2] = atom_int(arena, atom_string_len(map->original));
        value->expr.elems[3] = atom_int(arena, 0);
        value->expr.elems[4] = atom_int(arena, map->original_len);
    } else if (form(value, "ParserRejectedV1", 3u) || form(value, "ParserIncompleteV1", 3u)) {
        size_t scalar, byte;
        if (!number(value->expr.elems[1], &scalar) || scalar > map->cleaned_len ||
            !number(value->expr.elems[2], &byte) ||
            byte != byte_at(map->cleaned_units, map->cleaned_len, atom_string_len(map->cleaned), scalar)) return false;
        size_t original = locate_scalar(map, scalar, false);
        value->expr.elems[1] = atom_int(arena, original);
        value->expr.elems[2] = atom_int(arena, byte_at(map->original_units, map->original_len,
            atom_string_len(map->original), original));
    }
    return true;
}

typedef struct { Atom *input, *draft; CettaExprLen next; } LocationFrame;
static Atom *locate_tree(Arena *arena, Atom *tree, const SourceMap *map, bool *allocation) {
    LocationFrame *frames = NULL; size_t len = 0u, cap = 0u;
    PointerMap memo = {0}; Atom *input = tree, *result = NULL;
    for (;;) {
        if (!input) goto failed;
        if (input->kind != ATOM_EXPR) {
            result = atom_deep_copy(arena, input);
            if (!result) *allocation = true;
        }
        else {
            PointerCell *cell = pointer_find(&memo, input);
            if (cell) {
                if (!cell->value) goto failed;
                result = cell->value;
            } else {
                if (!pointer_add(&memo, input)) { *allocation = true; goto failed; }
                Atom *draft = atom_expr_builder_begin(arena, input->expr.len);
                if (!draft) { *allocation = true; goto failed; }
                if (input->expr.len) {
                    void *p = grow(frames, &cap, len + 1u, sizeof(*frames));
                    if (!p) { *allocation = true; goto failed; }
                    frames = p; frames[len++] = (LocationFrame){input, draft, 0u};
                    input = input->expr.elems[0]; continue;
                }
                result = atom_expr_builder_finish(arena, draft);
                pointer_find(&memo, input)->value = result;
            }
        }
        if (!result) goto failed;
        for (;;) {
            if (!len) { free(frames); free(memo.slots); return result; }
            LocationFrame *frame = &frames[len - 1u];
            frame->draft->expr.elems[frame->next++] = result;
            if (frame->next < frame->input->expr.len) {
                input = frame->input->expr.elems[frame->next]; break;
            }
            if (!locate_fields(arena, frame->draft, map)) goto failed;
            result = atom_expr_builder_finish(arena, frame->draft);
            if (!result) { *allocation = true; goto failed; }
            pointer_find(&memo, frame->input)->value = result;
            --len;
        }
    }
failed:
    free(frames); free(memo.slots); return NULL;
}

Atom *cetta_re_v1_locate_source(Arena *arena, Atom *observation, Atom *mapping) {
    if (form(mapping, "re:source-identity-v1", 1u) && string(mapping->expr.elems[1]))
        return atom_deep_copy(arena, observation);
    SourceMap map = {0}; bool allocation = false;
    if (!map_read(mapping, &map, &allocation)) {
        map_free(&map);
        return failure(arena, allocation ? "re:allocation-failure" : "re:source-fault",
            allocation ? "regex source-coordinate storage" : "invalid regex source-coordinate map", 0u);
    }
    bool forest = form(observation, "ParserWithForestV1", 2u);
    Atom *located = locate_tree(arena, forest ? observation->expr.elems[1] : observation, &map, &allocation);
    map_free(&map);
    if (!located) return failure(arena, allocation ? "re:allocation-failure" : "re:source-fault",
        allocation ? "regex source-coordinate traversal storage" : "invalid source-coordinate traversal", 0u);
    if (!forest) return located;
    Atom *provenance = atom_expr3(arena, atom_symbol(arena, "re:lexed-forest-v1"),
        atom_deep_copy(arena, observation->expr.elems[2]), atom_deep_copy(arena, mapping));
    return atom_expr3(arena, atom_symbol(arena, "ParserWithForestV1"), located, provenance);
}
