; `try` is one evaluation of a judgment, or of a program, with its outcome
; made explicit:
; the status and the evidence or reason.  Nothing is stored and nothing is
; re-run.  A `try` of a publishing judgment publishes nothing.

!(try (set:proves (pf:known nothing) Falsum))
!(try (set:proves (pf:hyp 0) Falsum))
!(try (set:proves (pf:all-elim (pf:known PowerI) Empty) (In Empty (Power Empty))))
!(try (set:formed (-> (x : set) (In x x))))
!(try (type:check (lam x x) u0))
(= (candidate) (superpose ((lam x x) u0)))
!(try (type:must (candidate) (-> u0 u0)))
!(try (lang:native-type zero (eval a) Nat))
; a program under try: evaluated once, its results reported
!(try (Power Empty))

; A try of a publishing judgment publishes nothing.
!(let $r (try (set:axiom ghost (In Empty Empty))) (car-atom $r))
!(set:proves (pf:known ghost) (In Empty Empty))

; A published fact carries the digest of the signature it was checked
; against; a record from another signature is not reused, and try says why.
!(add-atom &self (set:known stale (In Empty Empty) axiom () () 0 "0000"))
!(set:proves (pf:known stale) (In Empty Empty))
!(try (set:proves (pf:known stale) (In Empty Empty)))
