; HE conformance: unify
; Tests structural unification with success/failure branches

; unify succeeds — pattern variables bind
!(assertEqual (unify (A B) ($x $y) (found $x $y) (not-found)) (found A B))

; unify fails — returns else branch
!(assertEqual (unify (A B) (C D) success failure) failure)

; unify with nested structure
!(assertEqual (unify (f (g a)) (f $x) $x not-found) (g a))
