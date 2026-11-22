/* mgm_instance.c - the Megalodon kernel module compiled by rulec, as one instance of the rule runtime,
 * the components and the generic host glue: one translation unit whose global names are the
 * instance's own (generated/mgk_instance_names.h, written by tools/rulec/regen_megalodon_module.sh),
 * so that several compiled modules link into one binary, each with its own store.  Its only global
 * name left as it is: cetta_mgm_dispatch (mgm_instance.h). */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "generated/mgk_instance_names.h"
#include "../rule_runtime/rc_rt.c"
#include "../rule_components/rc_bytes.c"
#include "../rule_components/rc_intern.c"
#include "../rule_components/rc_memo.c"
#include "../rule_components/rc_order.c"
#include "../rule_components/rc_set.c"
#include "../rule_components/rc_table.c"
#include "generated/mgk_module.c"
#include "mgm_instance.h"
#define RCM_DISPATCH cetta_mgm_dispatch
#define RCM_OPS "__cetta_lib_mgm_"
#define RCM_TAG "mgm"
#include "../rule_module/rcm_cetta.c"
