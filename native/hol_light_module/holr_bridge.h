/* holr_bridge.h - between CeTTa's HOLR decoder and the compiled HOL Light kernel module.
 *
 * The decoder (hol_stream_scan) yields records; the module reads them as its Records sort through
 * the record cursor (an immediate index, viewed as rnil / rcons RECORD REST), so a record's term
 * is built only when the module reaches it.  Names and numerals are built in the representation
 * the compiled module was given (byte lists and binary numerals, or byte strings and immediates).
 */
#ifndef HOLR_BRIDGE_H
#define HOLR_BRIDGE_H

#include "../rule_runtime/rc_rt.h"
#include "../hol_light/hol_replay.h"

#include <stddef.h>
#include <stdint.h>

void holr_bridge_init(void);

/* The native kernel store, when the compiled module represents names, types and terms by it
 * (hol_light_module's representation layer): holr_store_begin starts a run (a new store, or the one
 * kept from the last run); holr_store_end ends it after the verdict has been read (empties the module
 * heap and collects the store, or frees it); holr_store_fresh(true) gives every run a new store;
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

/* module data as text, in the shape CeTTa prints MeTTa atoms ((ctor kid ...), nullary as (ctor)) */
typedef struct { char *s; size_t n, cap; } HbText;
void hb_put(HbText *b, const char *s, size_t n);
void hb_puts(HbText *b, const char *s);
void hb_nat_text(HbText *b, uint64_t v);
void hb_str_text(HbText *b, const uint8_t *p, size_t n);
void hb_term_text(HbText *b, const T *t, int sort);

#endif
