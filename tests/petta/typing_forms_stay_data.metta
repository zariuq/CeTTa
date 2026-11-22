; PeTTa's typing forms are ordinary data in every profile: `the`,
; `make-list`, `brand` and an arrow with a determinism mode name nothing
; that runs.  Extended adds only its `data` tuple.
!(the Marker (cons : (p s t)))
!(make-list a b)
!(data a b)
(: ContextualKB (Newtype Atom))
(: ContextualProof (Newtype Atom))
(: ContextualEvidence (Newtype Atom))
(: contextual-evidence
   (-[det]-> ContextualKB ContextualProof ContextualEvidence))
(= (contextual-evidence $kb $proof)
   (brand ContextualEvidence (evidence $kb $proof)))
!(contextual-evidence kb proof)

; A typed space's match pattern is literal structure.  A det declaration
; does not make a call inside the pattern a value to compute first, in a
; body or at the top level: (row (f 2)) does not meet the row (row 3).
(: &s (SpaceOf (Row Atom)))
(: f (-[det]-> Number Number))
(= (f $x) (+ $x 1))
!(add-atom &s (row 3))
(= (probe) (match &s (row (f 2)) yes))
(= (probe2 $y) (match &s (row (f $y)) (found $y)))
!(collapse (probe))
!(collapse (probe2 2))
!(collapse (match &s (row (f 2)) top))
!(collapse (match &s (row 3) three))
