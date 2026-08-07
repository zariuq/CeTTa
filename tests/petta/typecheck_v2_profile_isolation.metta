; Extended keeps the historical annotation erasure used by PeTTaChainer.
; It must not acquire the live typecheck-v2 meaning of `the` or `make-list`.
!(the Marker (cons : (p s t)))
!(make-list a b)

; The established extended-profile `data` tuple remains available.
!(data a b)

; Erased newtypes acquire their role from a declared argument position.
; This is contextual construction, not compatibility between distinct roles.
(: ContextualKB (Newtype Atom))
(: ContextualProof (Newtype Atom))
(: ContextualEvidence (Newtype Atom))
(: contextual-evidence
   (-[det]-> ContextualKB ContextualProof ContextualEvidence))
(= (contextual-evidence $kb $proof)
   (brand ContextualEvidence (evidence $kb $proof)))
!(contextual-evidence kb proof)
