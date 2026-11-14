/* rc_order.c - the structural order (see rc_order.h for the contract). */
#include "rc_order.h"
#include "rc_bytes.h"
#include "rc_nat.h"

int rc_order_cmp(int sort, T *a, T *b) {
    for (;;) {
        if (a == b) return 0;
        switch (rc_sort_order[sort]) {
        case RC_ORDER_BYTES: return rc_bytes_cmp(a, b);
        case RC_ORDER_NAT: return rc_nat_cmp(a, b);
        case RC_ORDER_RANK: return rc_ctor_rank[a->tag] < rc_ctor_rank[b->tag] ? -1 : 1;   /* a != b: two constructors */
        case RC_ORDER_STRUCT: break;
        default: rc_refinement_fail("a structural order on a sort without one");
        }
        RcINode *x = RC_INODE(a), *y = RC_INODE(b);
        if (x->ctor != y->ctor) return rc_ctor_rank[x->ctor] < rc_ctor_rank[y->ctor] ? -1 : 1;
#ifdef RC_FAULT_ORDER
        /* planted fault: the children of a two-child node compared right to left */
        if (x->n == 2) { int c = rc_order_cmp(rc_ctor_asort[rc_ctor_asort_off[x->ctor] + 1], x->kid[1], y->kid[1]); if (c) return c; }
#endif
        uint32_t n = x->n;
        if (n == 0) return 0;
        const uint16_t *as = rc_ctor_asort + rc_ctor_asort_off[x->ctor];
        for (uint32_t i = 0; i + 1 < n; i++) {
            int c = rc_order_cmp(as[i], x->kid[i], y->kid[i]);
            if (c) return c;
        }
        /* the last child: a loop, so long lists do not deepen the stack */
        sort = as[n - 1];
        a = x->kid[n - 1];
        b = y->kid[n - 1];
    }
}
