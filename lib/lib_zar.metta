; PeTTa-compatible top-level import path. Keep the actual compatibility
; definitions under lib/lib_petta so we can grow a Petta library lane without
; crowding the shared top-level lib namespace.

!(import! &self lib/lib_petta/lib_zar)
