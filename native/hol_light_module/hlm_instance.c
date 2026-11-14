/* hlm_instance.c - the HOL Light kernel module compiled by rulec, as one instance of the rule runtime,
 * the components, the stream bridge and the host glue: one translation unit whose global names are
 * the instance's own (generated/hol_instance_names.h, written by tools/rulec/regen_hol_light_module.sh),
 * so that several compiled modules link into one binary, each with its own store.  Its only global
 * name left as it is: cetta_holm_dispatch (holm_cetta_module.h).  Streams are read by the HOLR reader
 * compiled from its GSLT modules (native/holr_reader_module, through its table cetta_holr_reader);
 * of native/hol_light only the stream format's constants are used. */
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
#include "holr_bridge.c"
#include "holm_cetta_module.c"
