; Public binder conformance is stated through authored judgments. Exact ABT
; canonicalization and scope invariants are checked directly by the C gate.

(: Nat Type)

!(assertEqual (type:formed (-> u0 u0)) True)
!(assertEqual (type:eq (-> (x : u0) u0)
                          (-> (renamed : u0) u0)) True)
!(assertEqual (type:check (lam x x) (-> u0 u0)) True)
!(assertEqual (type:check (lam (x y) x) (-> u0 u0 u0)) True)
!(assertEqual (type:check (lam (x x) x) (-> u0 u0 u0)) True)
!(assertEqual (type:check (lam _ (idx 0)) (-> u0 u0)) True)
!(assertEqual (type:check (lam @1 @1) (-> u0 u0)) True)
!(assertEqual (type:check (lam λ λ) (-> u0 u0)) True)
!(assertEqual (type:check (lam x u0) (-> u0 u0)) False)
!(assertEqual (type:formed (-> (x y : u0 u0 u0) u0)) False)

!(println! (PrimeCanonicalBindersSummary 10 10 0))
