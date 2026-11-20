; Outside the typecheck profiles `the` is ordinary data, and extended does
; not acquire the live typecheck-v2 meaning of `the` or `make-list`.
!(the Marker (cons : (p s t)))
!(make-list a b)

; The extended-profile `data` tuple remains available.
!(data a b)

; The typecheck profiles erase `brand`, and their newtypes acquire a role
; from a declared argument position: contextual construction, not
; compatibility between distinct roles.  Elsewhere `brand` is ordinary data.
(: ContextualKB (Newtype Atom))
(: ContextualProof (Newtype Atom))
(: ContextualEvidence (Newtype Atom))
(: contextual-evidence
   (-[det]-> ContextualKB ContextualProof ContextualEvidence))
(= (contextual-evidence $kb $proof)
   (brand ContextualEvidence (evidence $kb $proof)))
!(contextual-evidence kb proof)
