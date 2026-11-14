/* hrm_instance.c - the HOLR stream reader compiled by rulec from its GSLT reader modules (HOLRStream,
 * theory Format), as one instance of the rule runtime and the components: one translation unit whose
 * global names are the instance's own (generated/holr_reader_instance_names.h, written by
 * tools/rulec/regen_holr_reader_module.sh).  Its only global name left as it is: the table of its
 * entry points, cetta_holr_reader (native/rule_module/rc_instance_api.h), through which the HOL Light
 * module's bridge reads streams. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "generated/holr_reader_instance_names.h"
#include "../rule_runtime/rc_rt.c"
#include "../rule_components/rc_bytes.c"
#include "../rule_components/rc_intern.c"
#include "../rule_components/rc_memo.c"
#include "../rule_components/rc_order.c"
#include "../rule_components/rc_set.c"
#include "../rule_components/rc_table.c"
#include "../rule_components/rc_bignat.c"
#include "../rule_components/rc_slices.c"
#include "generated/holr_reader.c"
#define RC_INSTANCE_API cetta_holr_reader
#define RC_INSTANCE_MODULE "HOLRStream"
#define RC_INSTANCE_THEORY "Format"
#include "../rule_module/rc_instance_api.c"
