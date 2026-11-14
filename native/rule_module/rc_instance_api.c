/* rc_instance_api.c - the table of entry points of one compiled module's instance (rc_instance_api.h).
 * Included last in the instance's translation unit, after the runtime, the components and the
 * generated code, with RC_INSTANCE_API defined as the table's (unprefixed) name. */
#include "rc_instance_api.h"

#ifndef RC_INSTANCE_API
#error "define RC_INSTANCE_API, the name of the instance's table"
#endif

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

const RcInstanceApi RC_INSTANCE_API = {
    RC_INSTANCE_MODULE, RC_INSTANCE_THEORY, rci_init, rci_ctor_named, rci_sort_named, rci_ctor_arity,
    rci_run_begin, rci_run_end, rci_free_store, rc_runtime_retain, rc_call, rc_view, rc_value_u64, rc_value_bytes, rc_bytes_input,
    rc_list_items,
};
