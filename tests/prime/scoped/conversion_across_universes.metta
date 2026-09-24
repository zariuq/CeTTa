; Equality of two typed terms does not depend on the universe their types were
; synthesized in, nor on how equal contexts are spelled.

; X : Sort 0.  The redex ((lam (Y : Sort 1). Y) X) synthesizes Sort 1 and X
; synthesizes Sort 0; by cumulativity both are types of Sort 1, and they are equal.
!(type:of (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (App (Lam (Sort (LevelConst 1)) (idx 0)) (idx 0))))
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (App (Lam (Sort (LevelConst 1)) (idx 0)) (idx 0)))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0)))
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                       (App (Lam (Sort (LevelConst 1)) (idx 0)) (idx 0))))

; Two distinct type variables are types of different universes and are not equal.
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 1))
                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 1))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 1))
                        (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0)))

; A type and a term of that type have no common type.
!(type:eq (PrimeScoped (PrimeCtxCons (idx 0) (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 1))
          (PrimeScoped (PrimeCtxCons (idx 0) (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)) (idx 0)))

; One context in two spellings: the variable is equal to itself.
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0))
          (PrimeScoped (PrimeCtxCons (App (Lam (Sort (LevelConst 1)) (idx 0))
                                          (Sort (LevelConst 0))) PrimeCtxNil) (idx 0)))

; Terms of two contexts that are not equal are not compared.
!(type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil) (idx 0))
          (PrimeScoped (PrimeCtxCons (Sort (LevelConst 1)) PrimeCtxNil) (idx 0)))
