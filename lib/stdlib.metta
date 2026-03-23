; CeTTa Standard Library — precompiled into binary at build time.
; These equations are loaded into &self before user code.
; They are introspectable via (match &self (= ...) ...).

; ── Type declarations ─────────────────────────────────────────────────
(: id (-> $t $t))
(: if (-> Bool Atom Atom $t))
(: car-atom (-> Expression Atom))
(: cdr-atom (-> Expression Expression))
(: size-atom (-> Expression Number))
(: index-atom (-> Expression Number Atom))

; ── Equations ─────────────────────────────────────────────────────────

; if: HE standard equations. Currently dispatched by the hardcoded handler
; in eval.c (for lazy branch evaluation + TCO). These equations exist in
; the space for meta-reasoning — they are NOT used for dispatch yet.
; TODO: fix handle_dispatch to support raw equation matching so these
; can replace the hardcoded handler.
(= (if True $then $else) $then)
(= (if False $then $else) $else)
