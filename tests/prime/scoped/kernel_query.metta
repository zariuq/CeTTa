; The represented query a judgment poses to the kernel, without an answer.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &self twice (-> nat nat)
   (= (twice zero) zero)
   (= (twice (succ $n)) (succ (succ (twice $n)))))
; Declared constants: their declarations form the context.
!(type:kernel-query (type:eq &self (twice (succ zero)) (succ (succ zero))))
!(type:kernel-query (type:check &self (twice zero) nat))
; Closed authored syntax: an empty context.
!(type:kernel-query (type:eq (-> (x : u0) u0) (-> (y : u0) u0)))
!(type:kernel-query (type:check (lam x x) (-> u0 u0)))
; A scoped query carries its own context; the two sides here have different
; synthesized types.
!(type:kernel-query
   (type:eq (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                         (Refl (idx 0)))
            (PrimeScoped (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
                         (App (Lam (Id (Sort (LevelConst 1)) (idx 0) (idx 0))
                                   (idx 0))
                              (Refl (idx 0))))))
; An undeclared head poses no kernel query.
!(type:kernel-query (type:eq Foo Foo))
; Asking for the query does not answer the judgment, and the judgment itself
; still decides.
!(type:eq &self (twice (succ zero)) (succ (succ zero)))
