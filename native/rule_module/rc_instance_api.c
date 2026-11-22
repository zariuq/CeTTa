/* rc_instance_api.c - the table of entry points of one compiled module's instance (rc_instance_api.h).
 * Included last in the instance's translation unit, after the runtime, the components and the
 * generated code, with RC_INSTANCE_API defined as the table's (unprefixed) name. */
#include "rc_instance_api.h"

#ifndef RC_INSTANCE_API
#error "define RC_INSTANCE_API, the name of the instance's table"
#endif

_Static_assert(RC_STOPPED_VALUE_BITS == RC_STOPPED_BITS, "the instance table's stopped value is the runtime's");

static bool rci_ready;
static void rci_init(void) {
    if (rci_ready) return;
    rc_runtime_init(0);
    rc_components_init();
    rci_ready = true;
}
static int rci_ctor_named(const char *name) {
    for (int c = 0; c < rc_nctors; c++) if (!strcmp(rc_ctor_name[c], name)) return c;
    return -1;
}
static int rci_sort_named(const char *name) {
    for (int s = 0; s < rc_nsorts; s++) if (!strcmp(rc_sort_name[s], name)) return s;
    return -1;
}
static int rci_ctor_arity(int c) { return c >= 0 && c < rc_nctors ? rc_ctor_arity[c] : -1; }
static int rci_ctor_sort(int c) { return c >= 0 && c < rc_nctors ? rc_ctor_sort[c] : -1; }
static int rci_ctor_arg_sort(int c, int i) {
    return c >= 0 && c < rc_nctors && i >= 0 && i < rc_ctor_arity[c] ? rc_ctor_asort[rc_ctor_asort_off[c] + i] : -1;
}
static void rci_run_begin(void) {
    rci_init();
    rc_runtime_reset();
    rc_intern_begin();
}
static void rci_run_end(void) {
    rc_runtime_reset();
    rc_intern_end();
}
static void rci_free_store(void) { rc_intern_free(); }
static void rci_set_fail_hook(void (*hook)(const char *what)) { rc_fail_hook = hook; }
static void rci_set_stop_hook(bool (*hook)(void)) { rc_stop_hook = hook; }
static T *rci_root(int i) { return i >= 0 && i < 8 ? rc_roots[i] : NULL; }
static size_t rci_stop_mark(void) { return rc_stop_mark; }
static void rci_pend_drop(size_t depth) { if (depth < rc_pend_depth) rc_pend_depth = depth; }

const RcInstanceApi RC_INSTANCE_API = {
    .module = RC_INSTANCE_MODULE, .theory = RC_INSTANCE_THEORY,
    .init = rci_init, .ctor_named = rci_ctor_named, .sort_named = rci_sort_named, .ctor_arity = rci_ctor_arity,
    .ctor_sort = rci_ctor_sort, .ctor_arg_sort = rci_ctor_arg_sort,
    .run_begin = rci_run_begin, .run_end = rci_run_end, .free_store = rci_free_store, .retain = rc_runtime_retain,
    .call = rc_call, .start = rc_start, .view = rc_view, .value_u64 = rc_value_u64, .value_bytes = rc_value_bytes, .bytes_input = rc_bytes_input,
    .value_items = rc_value_items, .list_items = rc_list_items,
    .set_fail_hook = rci_set_fail_hook, .set_stop_hook = rci_set_stop_hook, .resume = rc_resume, .root = rci_root,
    .stop_mark = rci_stop_mark, .frames = &rc_pend, .depth = &rc_pend_depth,
    .pend_drop = rci_pend_drop, .pend_apply = rc_pend_apply,
};
