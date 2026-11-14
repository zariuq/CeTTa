/* holr_bridge.h - between the HOLR stream reader and the compiled HOL Light kernel module.
 *
 * The reader is compiled by rulec from its GSLT reader modules (HOLRStream, theory Format): its own
 * instance in the binary (native/holr_reader_module), read through its table of entry points
 * (cetta_holr_reader).  Its value for a stream gives the header, the records decoded before the end
 * or the first fault, and how the stream ended; the bridge keeps the records, and the module reads
 * them as its Records sort through the record cursor (an immediate index, viewed as rnil / rcons
 * RECORD REST), so a record's term is built only when the module reaches it.  Names and numerals
 * are built in the representation the compiled module was given (byte lists and binary numerals, or
 * byte strings and immediates).  Of native/hol_light only the stream format's constants (opcodes,
 * flags, policies) are used.
 */
#ifndef HOLR_BRIDGE_H
#define HOLR_BRIDGE_H

#include "../rule_runtime/rc_rt.h"
#include "../hol_light/hol_replay.h"

#include <stddef.h>
#include <stdint.h>

void holr_bridge_init(void);

/* The interning store of the module's interned sorts (rc_intern.h) and its id tables (rc_table.h):
 * holr_store_begin starts a run (a new store, or the one kept from the last run); holr_store_end
 * ends it after the verdict has been read; holr_store_fresh(true) gives every run a new store;
 * holr_store_free frees the store and the module's constants. */
void holr_store_begin(void);
void holr_store_end(void);
void holr_store_fresh(bool fresh);
void holr_store_free(void);
bool holr_store_active(void);

/* Decode a whole stream; returns (hl:decoded CURSOR HEADER TERMINAL) in the young space.  The
 * records stay readable until the next decode. */
T *holr_bridge_decode(const uint8_t *bytes, size_t len);
size_t holr_bridge_records(void);

/* the host policy as the module's constant */
T *holr_bridge_policy(HolAxiomPolicy p);

/* the module's verdict for a decoded stream: hl:check HOST DECODED (NULL: no verdict) */
T *holr_bridge_check(T *host, T *decoded);


#endif
