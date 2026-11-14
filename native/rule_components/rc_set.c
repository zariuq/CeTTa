/* rc_set.c - ordered sets (see rc_set.h for the contract). */
#include "rc_set.h"

#include <stdlib.h>

typedef struct { T **v; size_t n, cap; T *small[32]; } Buf;
static void buf_init(Buf *b) { b->v = b->small; b->n = 0; b->cap = 32; }
static void buf_push(Buf *b, T *x) {
    if (b->n == b->cap) {
        size_t cap = 2 * b->cap;
        T **v = malloc(cap * sizeof *v);
        if (!v) rc_refinement_fail("memory for a set");
        memcpy(v, b->v, b->n * sizeof *v);
        if (b->v != b->small) free(b->v);
        b->v = v;
        b->cap = cap;
    }
    b->v[b->n++] = x;
}
static T *buf_onto(const RcSetDesc *d, Buf *b, T *tail) {
    for (size_t i = b->n; i-- > 0;) { T *k[2] = {b->v[i], tail}; tail = rc_intern_node(d->cons, 2, k); }
    if (b->v != b->small) free(b->v);
    return tail;
}
#define NO_ORDER 2       /* the comparator failed: the operation fails, as its rules do */
static int ord(const RcSetDesc *d, T *o) {
    if (!o) return NO_ORDER;
    uint16_t c = o->tag;   /* the order constructors are nullary: static terms */
    if (c == d->lt) return -1;
    if (c == d->gt) return 1;
    if (c == d->eq) return 0;
    rc_refinement_fail("a comparator gave no order");
}
static inline bool is_cons(const RcSetDesc *d, T *l) { return RC_INODE(l)->ctor == d->cons; }
static inline T *head(T *l) { return RC_INODE(l)->kid[0]; }
static inline T *tail(T *l) { return RC_INODE(l)->kid[1]; }

T *rc_set_union(const RcSetDesc *d, RcCmpFn cmp, T *l1, T *l2) {
    Buf out;
    buf_init(&out);
    T *a = l1, *b = l2;
    while (is_cons(d, a) && is_cons(d, b)) {
        int c = ord(d, cmp(head(a), head(b)));
        if (c == NO_ORDER) { if (out.v != out.small) free(out.v); return NULL; }
#ifdef RC_FAULT_SET
        c = -c;   /* planted fault: the larger head first */
#endif
        if (c == 0) { buf_push(&out, head(a)); a = tail(a); b = tail(b); }
        else if (c < 0) { buf_push(&out, head(a)); a = tail(a); }
        else { buf_push(&out, head(b)); b = tail(b); }
    }
    return buf_onto(d, &out, is_cons(d, a) ? a : b);
}

T *rc_set_remove(const RcSetDesc *d, RcCmpFn cmp, T *t, T *l) {
    Buf out;
    buf_init(&out);
    T *x = l;
    while (is_cons(d, x)) {
        int c = ord(d, cmp(t, head(x)));
        if (c == NO_ORDER) { if (out.v != out.small) free(out.v); return NULL; }
        if (c > 0) { buf_push(&out, head(x)); x = tail(x); continue; }
        if (c == 0) x = tail(x);
        break;
    }
    return buf_onto(d, &out, x);
}
