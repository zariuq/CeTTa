(hol:profile megalodon-hotg)

; `try` is one evaluation of a judgment, or of a program, with its outcome
; made explicit:
; the status and the evidence or reason.  Nothing is stored and nothing is
; re-run.  A `try` of a publishing judgment publishes nothing.

!(try (hol:proves (pf:known nothing) Falsum))
!(try (hol:proves (pf:hyp 0) Falsum))
!(try (hol:proves (pf:all-elim (pf:known universeIn) Empty) (In Empty (UnivOf Empty))))
!(try (hol:formed (-> (x : set) (In x x))))
!(try (type:check (lam x x) u0))
(= (candidate) (superpose ((lam x x) u0)))
!(try (type:must (candidate) (-> u0 u0)))
!(try (lang:native-type zero (eval a) Nat))
; a program under try: evaluated once, its results reported
!(try (Power Empty))

; A try of a publishing judgment publishes nothing.
!(let $r (try (hol:axiom ghost (In Empty Empty))) (car-atom $r))
!(hol:proves (pf:known ghost) (In Empty Empty))

; A published fact carries the digest of the signature it was checked
; against; a record from another signature is not reused, and try says why.
!(add-atom &self (hol:known stale (In Empty Empty) axiom () () 0 "0000"))
!(hol:proves (pf:known stale) (In Empty Empty))
!(try (hol:proves (pf:known stale) (In Empty Empty)))
