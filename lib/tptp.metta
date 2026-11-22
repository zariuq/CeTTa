; The native TPTP module.  Import with (import! &self tptp).
; It reads TPTP with the tables of the official grammar's snapshot: readers
; of the strict and the corpus-compatible profile, the canonical term of each
; input they read, its compact records under a leaf policy, and records and
; canonical terms printed back to text.  Its operations are the
; __cetta_lib_tptp_ natives.  The public TPTP library, lib_tptp, imports this
; module and gives them their public names (tptp:...).
