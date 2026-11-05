/*
 * Spans in the runtime of the C target (gslt_native_runtime_v1.h): a span
 * is the list (Cell (cp N) ... End) of the scalars it covers, for matching,
 * equality, hashing (through equality of applications that hold one),
 * printing and copying out; built in front of, it extends only over the
 * scalar's own bytes; a class scan returns one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native/gslt_native_runtime_v1.h"

static const GnOpV1 ops[] = {{"cons", 2u}, {"nil", 0u}, {"cp", 1u}, {"Cell", 2u}, {"End", 0u}, {"Box", 1u}};
static const GnProgramV1 program = {ops, 6u, GN_OP_CONS, GN_OP_NIL, GN_OP_CP};
enum { CELL = GN_FIRST_OP + 3, END = GN_FIRST_OP + 4, BOX = GN_FIRST_OP + 5 };

static int checks, failures;

static void check(bool ok, const char *what) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static GnVal *cp(GnCtx *cx, uint32_t c) { return gn_app1(cx, GN_OP_CP, gn_int(cx, c)); }

/* The cells (Cell (cp s0) ... End). */
static GnVal *cells(GnCtx *cx, const uint32_t *s, size_t n) {
    GnVal *v = gn_const(cx, END);
    while (n--)
        v = gn_app2(cx, CELL, cp(cx, s[n]), v);
    return v;
}

static char *printed(GnCtx *cx, const GnVal *v) {
    char *text = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&text, &len);
    gn_print(cx, out, v);
    fclose(out);
    return text;
}

static bool same_print(GnCtx *cx, const GnVal *a, const GnVal *b) {
    char *x = printed(cx, a), *y = printed(cx, b);
    bool same = strcmp(x, y) == 0;
    free(x);
    free(y);
    return same;
}

static void run(GnCtx *cx) {
    /* "l" "é" "t", then a byte that is not UTF-8 (read as U+DCC3), then "!" */
    static const uint8_t text[] = {'l', 0xC3, 0xA9, 't', 0xC3, '!'};
    GnSourceV1 src = {text, sizeof text};
    const uint32_t all[] = {'l', 0xE9, 't', 0xDCC3, '!'};
    GnVal *span = gn_span(cx, &src, 0u, sizeof text, CELL, END);
    GnVal *list = cells(cx, all, 5u);

    check(span->op == GN_SPAN, "a nonempty stretch is a span");
    check(gn_span(cx, &src, 3u, 3u, CELL, END) == gn_const(cx, END), "an empty stretch is End itself");
    check(gn_eq(cx, span, list) && gn_eq(cx, list, span), "a span equals the cells it reads as");
    check(gn_eq(cx, gn_app1(cx, BOX, span), gn_app1(cx, BOX, list)), "and hashes as they do");
    const uint32_t other[] = {'l', 0xE9, 't', 0xDCC3, '?'};
    check(!gn_eq(cx, span, cells(cx, other, 5u)), "a span differs from other cells");
    check(!gn_eq(cx, gn_app1(cx, BOX, span), gn_app1(cx, BOX, cells(cx, other, 5u))), "inside an application too");
    GnSourceV1 copy = {(const uint8_t[]){'l', 0xC3, 0xA9, 't', 0xC3, '!'}, 6u};
    check(gn_eq(cx, span, gn_span(cx, &copy, 0u, 6u, CELL, END)), "spans of equal bytes are equal");
    check(!gn_eq(cx, span, gn_span(cx, &src, 0u, 5u, CELL, END)), "a shorter span is not");
    GnVal *mixed = gn_app2(cx, CELL, cp(cx, 'l'), gn_span(cx, &src, 1u, sizeof text, CELL, END));
    check(gn_eq(cx, mixed, span) && gn_eq(cx, span, mixed), "a cell before a span equals the whole span");
    check(same_print(cx, span, list), "a span prints as its cells");

    GnVal *h, *t;
    check(gn_uncons_op(cx, span, CELL, &h, &t) && gn_eq(cx, h, cp(cx, 'l')) &&
              gn_eq(cx, t, cells(cx, all + 1, 4u)), "a span taken apart: its first scalar and the rest");
    check(!gn_uncons_op(cx, span, BOX, &h, &t), "a span is no other constructor");
    check(gn_op_of(span) == CELL, "a span dispatches as a cell");

    /* built in front of a span: over the scalar's own bytes only */
    GnVal *rest = gn_span(cx, &src, 1u, sizeof text, CELL, END);
    GnVal *grown = gn_cons_cp(cx, CELL, cp(cx, 'l'), rest);
    check(grown->op == GN_SPAN && grown->i == 0, "l before its span extends it");
    check(gn_cons_cp(cx, CELL, cp(cx, 'x'), rest)->op == CELL, "x before it is a cell");
    /* the byte 0xC3 alone reads as U+DCC3, but before 0xA9 it begins é */
    GnVal *after_c3 = gn_span(cx, &src, 2u, 4u, CELL, END);
    GnVal *front = gn_cons_cp(cx, CELL, cp(cx, 0xDCC3), after_c3);
    check(front->op == CELL, "an escaped byte that would begin a longer scalar is a cell");
    const uint32_t odd[] = {0xDCC3, 0xDCA9, 't'};
    check(gn_eq(cx, front, cells(cx, odd, 3u)), "and reads as what was built");
    check(gn_cons_cp(cx, CELL, cp(cx, 0xE9), gn_span(cx, &src, 3u, 4u, CELL, END))->op == GN_SPAN,
          "é before its span extends it");

    /* literal texts */
    const uint32_t let[] = {'l', 0xE9, 't'};
    GnVal *let_span = gn_span(cx, &src, 0u, 4u, CELL, END);
    check(gn_is_scalars(cx, let_span, CELL, END, let, 3u), "a span is its scalars");
    check(gn_is_text(cx, let_span, CELL, END, let, 3u, "l\xc3\xa9t", 4u), "and its bytes");
    check(!gn_is_text(cx, let_span, CELL, END, let, 2u, "l\xc3\xa9", 3u), "not a prefix of them");
    check(gn_is_text(cx, cells(cx, let, 3u), CELL, END, let, 3u, "l\xc3\xa9t", 4u), "cells are their text too");
    check(!gn_is_scalars(cx, let_span, CELL, END, odd, 3u), "not the escaped bytes of é");

    /* a class scan */
    GnClassV1 letters = {{0, (uint64_t)1 << ('l' - 64) | (uint64_t)1 << ('t' - 64)}, (const uint32_t[][2]){{0xE9, 0xE9}}, 1u};
    GnVal *run, *after, *cursor = gn_cursor(cx, &src, 0u);
    check(gn_scan_class(cx, cursor, &letters, CELL, END, &run, &after), "a scan of a source list");
    check(gn_eq(cx, run, cells(cx, all, 3u)) && after->op == GN_CURSOR && after->i == 4,
          "takes l é t, stops at the escaped byte");
    check(gn_scan_class(cx, after, &letters, CELL, END, &run, &after) && run == gn_const(cx, END) && after->i == 4,
          "takes nothing where the class does not begin");
    check(gn_scan_class(cx, gn_const(cx, GN_OP_NIL), &letters, CELL, END, &run, &after) &&
              run == gn_const(cx, END), "takes nothing from nil");
    check(!gn_scan_class(cx, list, &letters, CELL, END, &run, &after), "declines a list that is not the source");

    /* copied out, a span keeps what it reads */
    GnVal *kept = gn_keep_result(cx, gn_app1(cx, BOX, span));
    gn_scratch_reset(cx);
    check(kept->kid[0]->op == GN_SPAN && gn_eq(cx, kept->kid[0], cells(cx, all, 5u)), "a kept span reads the same");
}

int main(void) {
    GnCtx *cx = gn_ctx_new(&program);
    if (!cx)
        return 2;
    if (!GN_TRY(cx, run(cx)))
        check(false, "out of memory");
    gn_ctx_free(cx);
    printf("(GsltNativeSpans %d passed %d failed)\n", checks - failures, failures);
    return failures ? 1 : 0;
}
