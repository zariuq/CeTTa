; A refutation of type:eq or type:check compares a synthesized type with
; another. It is a theorem only when the synthesized type is principal:
; below every type of the term.

; X : Sort 0.  refl X has type Id (Sort 0) X X and Id (Sort 1) X X, neither
; below the other.  The two terms below are equal at Id (Sort 1) X X: the left
; side checks at the right side's synthesized type, so they are compared
; there, and the mismatch of their synthesized types refutes nothing.
!(type:check (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (Refl (idx 0)))
             (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (Refl (idx 0)))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
            (App (Lam (Id (Sort (LevelConst 1)) (idx 0) (idx 0)) (idx 0)) (Refl (idx 0)))))

; X, Y : Sort 0, x : X, y : Y.  Variables have principal types: x and y are
; equal at no type, and x does not have type Y.
!(type:eq (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 1)
            (PrimeCtxCons (Sort (LevelConst 0)) (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)))) (idx 1))
          (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 1)
            (PrimeCtxCons (Sort (LevelConst 0)) (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)))) (idx 0)))
!(type:check (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 1)
               (PrimeCtxCons (Sort (LevelConst 0)) (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)))) (idx 1))
             (idx 2))

; X : Sort 0, a b : X.  An abstraction applied to an argument of the
; non-universe type X has a principal type: (lam (n : X). n) a is not b.
!(type:eq (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 0)
            (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (App (Lam (idx 2) (idx 0)) (idx 1)))
          (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 0)
            (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (idx 0)))
!(type:eq (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 0)
            (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (App (Lam (idx 2) (idx 0)) (idx 1)))
          (PrimeScoped (PrimeCtxCons (idx 1) (PrimeCtxCons (idx 0)
            (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil))) (idx 1)))
