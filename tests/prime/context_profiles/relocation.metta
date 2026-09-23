; Three-space relocation.  A carries a policy and a reading; B and C carry
; readings.  The same program is run under three conventions for what the
; short spelling &self denotes inside code evaluated in another space:
; the program's space, the space the code was captured in, or the space the
; evaluation is in.  &home and &here name the two roles explicitly and mean
; the same thing under every convention.
!(context:self)
!(bind! &A (new-space))
!(bind! &B (new-space))
!(bind! &C (new-space))
!(add-atom &A (reading 17))
!(add-atom &A (policy strict))
!(add-atom &B (reading 23))
!(add-atom &C (reading 31))
!(add-atom &self (reading 5))

; 1. Explicit roles never depend on the convention: a captured pair read.
!(let $code (ctx:capture &A (Pair (match &home (reading $x) $x) (match &here (reading $y) $y)))
   (Pair (eval $code &B) (eval $code &C)))
; 2. The same code run where it stands: here is this space.
!(let $code (ctx:capture &A (Pair (match &home (reading $x) $x) (match &here (reading $y) $y)))
   (run $code))
; 3. Nested: enter B, temporarily enter C, return to B.
!(let $inner (ctx:capture &A (match &here (reading $y) $y))
   (eval (Triple (match &here (reading $y1) $y1) (eval $inner &C) (match &here (reading $y2) $y2)) &B))
; 4. Raw syntax fetched from A carries no home: its &home is the program's
;    space outside captured code, and its &here is where it runs.
!(add-atom &A (code (Pair (match &home (reading $x) $x) (match &here (reading $y) $y))))
!(match &A (code $c) (eval $c &B))
; 5. The ambiguous spelling: &self inside captured code evaluated in B.
!(let $code (ctx:capture &A (match &self (reading $x) $x)) (eval $code &B))
; 6. The ambiguous spelling in raw syntax evaluated in B, and at top level.
!(eval (match &self (reading $x) $x) &B)
!(match &self (reading $x) $x)
; 7. Independence from the spelling of B: the captured code never names B.
!(bind! &B2 (new-space))
!(add-atom &B2 (reading 23))
!(let $code (ctx:capture &A (Pair (match &home (policy $p) $p) (match &here (reading $y) $y)))
   (eval $code &B2))
; 8. Two representations of a reference: a retained handle keeps the space it
;    was taken from when the registry name is rebound; the symbolic name
;    follows the rebinding.  Independent of the convention.
;    Storing syntax keeps syntax: the handle must be taken before it is stored.
!(let $h (ctx:capture &A (match &home (reading $x) $x)) (add-atom &self (keep-handle $h)))
!(add-atom &self (keep-name (captured &A (match &home (reading $x) $x))))
!(bind! &A (new-space))
!(add-atom &A (reading 99))
!(match &self (keep-handle $h) (run $h))
!(match &self (keep-name $c) (run $c))
