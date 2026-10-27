#include "atom.h"
#include "str_natives.h"
#include "utf8.h"
#include "stats.h"
#if CETTA_BUILD_WITH_RUNTIME_STATS
#include "tests/test_runtime_stats_stubs.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define check(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

static Atom *invoke(Arena *arena, const char *name, Atom **args) {
    const CettaStrNative *native = cetta_str_native(name);
    CettaStrRefusal refusal = {0};
    assert(native);
    Atom *result = native->fn(arena, args, &refusal);
    if (!result) {
        fprintf(stderr, "unexpected %s refusal: %s %s at %zu\n", name,
                refusal.reason ? refusal.reason : "", refusal.expected ? refusal.expected : "", refusal.offset);
        abort();
    }
    return result;
}

static Atom *feed(Arena *arena, Atom *state, const char *bytes, size_t size, bool final) {
    return invoke(arena, "utf8-feed", (Atom *[]){state, atom_string_n(arena, bytes, size), atom_bool(arena, final)});
}

static bool same_bytes(Atom *value, const char *bytes, size_t length) {
    return value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING &&
        atom_string_len(value) == length && !memcmp(value->ground.sval, bytes, length);
}

int main(void) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_enable();
#endif
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
#endif
    Atom *ignore = invoke(&arena, "utf8-new", (Atom *[]){atom_symbol(&arena, "ignore")});
    Atom *strict = invoke(&arena, "utf8-new", (Atom *[]){atom_symbol(&arena, "strict")});
    ArenaMark mark = arena_mark(&arena);

    /* Every two-byte buffer under ignore, split at every boundary. This is
     * partition consistency; Python separately qualifies decoding semantics. */
    for (unsigned first = 0; first < 256u; first++) {
        for (unsigned second = 0; second < 256u; second++) {
            char bytes[2] = {(char)first, (char)second};
            Atom *whole = feed(&arena, ignore, bytes, sizeof(bytes), true);
            for (size_t split = 0u; split <= sizeof(bytes); split++) {
                Atom *prefix = feed(&arena, ignore, bytes, split, false);
                Atom *suffix = feed(&arena, prefix->expr.elems[2], bytes + split, sizeof(bytes) - split, true);
                Atom *left = prefix->expr.elems[1], *right = suffix->expr.elems[1], *expected = whole->expr.elems[1];
                size_t a = atom_string_len(left), b = atom_string_len(right);
                check(a + b == atom_string_len(expected));
                check(!memcmp(left->ground.sval, expected->ground.sval, a));
                check(!memcmp(right->ground.sval, expected->ground.sval + a, b));
                check(atom_eq(suffix->expr.elems[2], whole->expr.elems[2]));
            }
            arena_reset(&arena, mark);
        }
    }
    const uint32_t scalars[] = {0u, 0x7fu, 0x80u, 0x7ffu, 0x800u, 0xd7ffu, 0xe000u,
        0xffffu, 0x10000u, 0x10ffffu, 0x130u, 0x131u, 0x17fu, 0x212au, 0x2028u, 0x2029u};
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++) {
        uint8_t bytes[4];
        size_t size = cetta_utf8_encode(scalars[i], bytes);
        for (size_t split = 0u; split <= size; split++) {
            Atom *prefix = feed(&arena, strict, (char *)bytes, split, false);
            Atom *suffix = feed(&arena, prefix->expr.elems[2], (char *)bytes + split, size - split, true);
            Atom *left = prefix->expr.elems[1], *right = suffix->expr.elems[1];
            size_t a = atom_string_len(left), b = atom_string_len(right);
            check(a + b == size);
            check(!memcmp(left->ground.sval, bytes, a) && !memcmp(right->ground.sval, bytes + a, b));
            check(atom_string_len(suffix->expr.elems[2]->expr.elems[2]) == 0u);
        }
        arena_reset(&arena, mark);
    }
    const CettaStrNative *utf = cetta_str_native("utf8-feed");
    CettaStrRefusal refusal = {0};
    Atom *pending = feed(&arena, strict, "a\xe2\x80", 3u, false);
    check(!utf->fn(&arena, (Atom *[]){pending->expr.elems[2], atom_string(&arena, ""), atom_true(&arena)}, &refusal));
    check(!strcmp(refusal.reason, "StrMalformedUtf8V1") && refusal.offset == 1u);
    check(same_bytes(pending->expr.elems[1], "a", 1u));

    Atom *buffer = invoke(&arena, "line-new", NULL);
    size_t number = 0u;
    const char *pieces[] = {"first\r", "", "\n\nlast"};
    bool preceding_cr = false;
    for (size_t i = 0u; i < 3u; i++) {
        Atom *segments = invoke(&arena, "line-segments", (Atom *[]){atom_string(&arena, pieces[i]), atom_bool(&arena, preceding_cr)});
        Atom *collected = invoke(&arena, "collect-lines", (Atom *[]){buffer, segments, atom_bool(&arena, i == 2u)});
        Atom *fused = invoke(&arena, "feed-lines",
            (Atom *[]){buffer, atom_string(&arena, pieces[i]), atom_bool(&arena, i == 2u)});
        Atom *advanced = invoke(&arena, "advance-lines",
            (Atom *[]){buffer, atom_string(&arena, pieces[i]), atom_bool(&arena, i == 2u)});
        check(atom_eq(fused, collected));
        check(atom_eq(advanced, collected->expr.elems[2]));
        for (Atom *rows = collected->expr.elems[1]; !atom_is_symbol(rows, "LNil"); rows = rows->expr.elems[2]) {
            Atom *record = rows->expr.elems[1];
            const char *expected[] = {"first", "", "last"};
            check(number < 3u && same_bytes(record->expr.elems[2], expected[number], strlen(expected[number])));
            check(record->expr.elems[1]->ground.ival == (int64_t)++number);
        }
        buffer = collected->expr.elems[2];
        preceding_cr = buffer->expr.elems[3]->ground.bval;
    }
    check(number == 3u && atom_is_symbol(buffer->expr.elems[1], "LNil"));

    /* Long partial lines retain fragments; no growing string is assembled
     * before its completion. Moving the states retains those fragments. */
    Arena current, next;
    arena_init_detached(&current);
    arena_init_detached(&next);
    Atom *long_buffer = invoke(&current, "line-new", NULL);
    char part[1024]; memset(part, 'x', sizeof(part));
    for (size_t i = 0; i < 1024u; i++) {
        Atom *segments = invoke(&current, "line-segments", (Atom *[]){atom_string_n(&current, part, sizeof(part)), atom_false(&current)});
        Atom *collected = invoke(&current, "collect-lines", (Atom *[]){long_buffer, segments, atom_false(&current)});
        check(atom_is_symbol(collected->expr.elems[1], "LNil"));
        /* Keep the pending graph, without retaining superseded output rows. */
        if (i % 64u == 63u) {
            long_buffer = atom_deep_copy(&next, collected->expr.elems[2]);
            arena_free(&current);
            current = next;
            arena_init_detached(&next);
        } else long_buffer = collected->expr.elems[2];
    }
    Atom *segments = invoke(&current, "line-segments", (Atom *[]){atom_string(&current, ""), atom_false(&current)});
    Atom *complete = invoke(&current, "collect-lines", (Atom *[]){long_buffer, segments, atom_true(&current)});
    Atom *line = complete->expr.elems[1]->expr.elems[1]->expr.elems[2];
    check(atom_string_len(line) == 1024u * sizeof(part));
    check(line->ground.sval[0] == 'x' && line->ground.sval[atom_string_len(line) - 1u] == 'x');
    arena_free(&current);
    arena_free(&next);

    /* Cyclic forged lists are refused where traversed, without scanning an
     * already buffered prefix on every ordinary non-final feed. */
    Atom *cycle = atom_expr3(&arena, atom_symbol(&arena, "LCons"),
        atom_string(&arena, "pending"), atom_symbol(&arena, "LNil"));
    cycle->expr.elems[2] = cycle;
    Atom *cyclic_buffer = atom_expr(&arena, (Atom *[]){atom_symbol(&arena, "LineBufferV1"),
        cycle, atom_int(&arena, 0), atom_false(&arena)}, 4u);
    Atom *empty_segments = invoke(&arena, "line-segments",
        (Atom *[]){atom_string(&arena, ""), atom_false(&arena)});
    const CettaStrNative *collector = cetta_str_native("collect-lines");
    refusal = (CettaStrRefusal){0};
    check(!collector->fn(&arena, (Atom *[]){cyclic_buffer, empty_segments, atom_true(&arena)}, &refusal));
    check(refusal.expected && strstr(refusal.expected, "acyclic"));
    Atom *part_cycle = atom_expr3(&arena, atom_symbol(&arena, "LCons"),
        atom_expr3(&arena, atom_symbol(&arena, "LinePartV1"), atom_string(&arena, ""), atom_false(&arena)),
        atom_symbol(&arena, "LNil"));
    part_cycle->expr.elems[2] = part_cycle;
    Atom *cyclic_segments = atom_expr3(&arena, atom_symbol(&arena, "LineSegmentsV1"),
        part_cycle, atom_false(&arena));
    refusal = (CettaStrRefusal){0};
    check(!collector->fn(&arena, (Atom *[]){buffer, cyclic_segments, atom_false(&arena)}, &refusal));
    check(refusal.expected && strstr(refusal.expected, "acyclic"));

    Atom *found = invoke(&arena, "find-ascii-uncased", (Atom *[]){atom_string(&arena, "İıſK"), atom_string(&arena, "K")});
    check(found->ground.ival == 6);
    found = invoke(&arena, "find-ascii-uncased", (Atom *[]){atom_string(&arena, "xababababac"), atom_string(&arena, "ABABAC")});
    check(found->ground.ival == 5);
    found = invoke(&arena, "find-ascii-uncased", (Atom *[]){atom_string_n(&arena, "a\xff" "b", 3u), atom_string(&arena, "ab")});
    check(found->kind == ATOM_EXPR && atom_is_symbol(found->expr.elems[0], "TextNotFound"));
    arena_free(&arena);
    symbol_table_free(&symbols);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_ARENA_ALLOC_BYTES] > 0u);
    puts("PASS: text allocations are observed by runtime statistics");
#endif
    printf("(TextStreamingNativeV1Summary %u 0)\n", checks);
    return 0;
}
