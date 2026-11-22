/* hlm_instance.c - the HOL Light kernel module compiled by rulec, as one instance of the rule runtime,
 * the components and the generic host glue (native/rule_module/rcm_cetta.c): one translation unit
 * whose global names are the instance's own (generated/hol_instance_names.h, written by
 * tools/rulec/regen_hol_light_module.sh), so that several compiled modules link into one binary, each
 * with its own store.  Its only global name left as it is: cetta_holm_dispatch (holm_cetta_module.h).
 * Streams are read by the HOLR reader compiled from its GSLT modules (native/holr_reader_module,
 * through its table cetta_holr_reader) and fed to the module as its records while it runs
 * (native/rule_module/rc_feed.c, described by hlm_feed.c); of native/hol_light only the stream format's
 * constants are used. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "generated/hol_instance_names.h"
#include "../rule_runtime/rc_rt.c"
#include "../rule_components/rc_bytes.c"
#include "../rule_components/rc_intern.c"
#include "../rule_components/rc_memo.c"
#include "../rule_components/rc_order.c"
#include "../rule_components/rc_set.c"
#include "../rule_components/rc_table.c"
#include "generated/hol_module.c"
#include "../rule_module/rc_feed.c"
#include "hlm_feed.c"
#include "holm_cetta_module.h"
#define RCM_DISPATCH cetta_holm_dispatch
#define RCM_OPS "__cetta_lib_holm_"
#define RCM_TAG "holm"
#define RCM_FEED hol_stream_feed
/* the rules recurse on the depth of terms and the length of hypothesis lists */
#define RCM_STACK_BYTES ((size_t)1 << 30)
#include "../rule_module/rcm_cetta.c"
