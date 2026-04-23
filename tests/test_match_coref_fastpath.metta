; Coreferential flat/path matching should keep repeated-variable constraints
; inside the backend walk when the repeated subtrees are structurally the same.
; It must still preserve the older, more flexible unification semantics by
; falling back when repeated occurrences only become equal after indexed-side
; variable instantiation.

!(bind! &kb (new-space))
!(add-atom &kb (pair A A))
!(add-atom &kb (pair A B))
!(add-atom &kb (pair $z $z))
!(add-atom &kb (pair $u (f $u)))

; Match result order is backend-specific here; the contract is the set of
; alpha-equivalent solutions, including the indexed-side variable witness.
!(assertAlphaEqual
  (match &kb (pair $x $x) $x)
  (superpose (A $z)))

!(assertAlphaEqualToResult
  (match &kb (pair $x (f $x)) $x)
  ($u))

!(bind! &fb (new-space))
!(add-atom &fb (pair $w A))

!(assertAlphaEqualToResult
  (match &fb (pair $x $x) $x)
  (A))

!(bind! &gb (new-space))
!(add-atom &gb (pair $w A))
!(add-atom &gb (pair B $u))

!(assertAlphaEqual
  (match &gb (pair $x $x) $x)
  (superpose (A B)))
