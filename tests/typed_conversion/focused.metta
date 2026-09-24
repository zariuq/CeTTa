!(import! &self ../../lib/typed_conversion.metta)

!(R inst0 (tc:inst0 none (DeclConst z) (idx 0)))
!(R wk (tc:wk none (idx 0)))
!(R under (tc:inst0 none (idx 0) (Lam (DeclConst A) (idx 1))))
!(R beta
   (tc:compare 40 none LNil Nil Nil
     (App (Lam (DeclConst A) (idx 0)) (DeclConst z))
     (DeclConst z)
     (DeclConst A)))
!(R beta-shift
   (tc:compare 40 none LNil Nil Nil
     (App (Lam (DeclConst A) (Lam (DeclConst A) (idx 1))) (idx 0))
     (Lam (DeclConst A) (idx 1))
     (Pi (DeclConst A) (Pi (DeclConst A) (DeclConst A)))))
!(R fst
   (tc:compare 40 none LNil Nil Nil
     (Fst (Pair (DeclConst a) (DeclConst b)))
     (DeclConst a)
     (DeclConst A)))
!(R delta
   (tc:compare 40 none
     (LCons (PrimeRule f 2 ((PVar 0) (PVar 1)) (PVar 0)) LNil)
     Nil Nil
     (App (App (DeclConst f) (DeclConst a)) (DeclConst b))
     (DeclConst a)
     (DeclConst A)))
!(R delta-short
   (tc:compare 40 none
     (LCons (PrimeRule f 2 ((PVar 0) (PVar 1)) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst f) (DeclConst a))
     (DeclConst a)
     (DeclConst A)))
!(R pi-eta
   (tc:compare 40 none LNil
     (Cons (Pi (DeclConst A) (DeclConst A)) Nil)
     Nil
     (idx 0)
     (Lam (DeclConst A) (App (idx 1) (idx 0)))
     (Pi (DeclConst A) (DeclConst A))))
!(R sigma-eta
   (tc:compare 40 none LNil
     (Cons (Sigma (DeclConst A) (DeclConst A)) Nil)
     Nil
     (idx 0)
     (Pair (Fst (idx 0)) (Snd (idx 0)))
     (Sigma (DeclConst A) (DeclConst A))))
!(R fuel (tc:compare 0 none LNil Nil Nil (DeclConst a) (DeclConst a) (DeclConst A)))
!(R distinct
   (tc:compare 20 none LNil Nil Nil (DeclConst a) (DeclConst b) (DeclConst A)))
!(R cumul
   (tc:compare 20 none LNil
     (Cons (Sort (LevelConst 0)) Nil)
     Nil
     (idx 0) (idx 0)
     (Sort (LevelConst 1))))
!(R no-cumul
   (tc:compare 20 no-cumulativity LNil
     (Cons (Sort (LevelConst 0)) Nil)
     Nil
     (idx 0) (idx 0)
     (Sort (LevelConst 1))))
!(R pi-sigma
   (tc:compare 20 none LNil Nil Nil
     (Pi (DeclConst A) (DeclConst A))
     (Sigma (DeclConst A) (DeclConst A))
     (Sort (LevelConst 0))))
!(R refl
   (tc:compare 20 none LNil Nil Nil
     (Refl (DeclConst a))
     (Refl (DeclConst a))
     (Id (DeclConst A) (DeclConst a) (DeclConst a))))
!(R drop-pi
   (tc:compare 30 drop-pi-eta LNil
     (Cons (Pi (DeclConst A) (DeclConst A)) Nil)
     Nil
     (idx 0)
     (Lam (DeclConst A) (App (idx 1) (idx 0)))
     (Pi (DeclConst A) (DeclConst A))))
!(R iota
   (tc:compare 40 none
     (LCons (PrimeRule j 1 ((Refl (PVar 0))) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst j) (Refl (DeclConst a)))
     (DeclConst a)
     (DeclConst A)))
!(R iota-stuck
   (tc:compare 40 none
     (LCons (PrimeRule j 1 ((Refl (PVar 0))) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst j) (idx 0))
     (DeclConst a)
     (DeclConst A)))
!(R drop-sigma
   (tc:compare 40 drop-sigma-eta LNil
     (Cons (Sigma (DeclConst A) (DeclConst A)) Nil)
     Nil
     (idx 0)
     (Pair (Fst (idx 0)) (Snd (idx 0)))
     (Sigma (DeclConst A) (DeclConst A))))
!(R below
   (tc:compare 40 delta-below-arity
     (LCons (PrimeRule f 2 ((PVar 0) (PVar 1)) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst f) (DeclConst a))
     (DeclConst a)
     (DeclConst A)))
!(R shift-fault
   (tc:compare 40 wrong-shift LNil Nil Nil
     (App (Lam (DeclConst A) (Lam (DeclConst A) (idx 1))) (idx 0))
     (Lam (DeclConst A) (idx 1))
     (Pi (DeclConst A) (Pi (DeclConst A) (DeclConst A)))))
!(R endpoint
   (tc:compare 40 none
     (LCons (PrimeRule j 2 ((PVar 0) (PVar 0)) (PVar 0)) LNil)
     Nil Nil
     (App (App (DeclConst j) (DeclConst a)) (DeclConst b))
     (DeclConst a)
     (DeclConst A)))
!(R skip-endpoint
   (tc:compare 40 skip-endpoint
     (LCons (PrimeRule j 2 ((PVar 0) (PVar 0)) (PVar 0)) LNil)
     Nil Nil
     (App (App (DeclConst j) (DeclConst a)) (DeclConst b))
     (DeclConst a)
     (DeclConst A)))
!(R scoped-beta
   (tc:compare 20 none LNil
     (PrimeCtxCons (Sort (LevelConst 0)) PrimeCtxNil)
     Nil
     (idx 0)
     (App (Lam (Sort (LevelConst 0)) (idx 0)) (idx 0))
     (Sort (LevelConst 0))))
!(R j-neutral
   (tc:compare 40 none
     (LCons (PrimeRule j 1 ((Refl (PVar 0))) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst j) (idx 0))
     (idx 0)
     (DeclConst A)))
!(R j-neutral-fault
   (tc:compare 40 fire-j-neutral
     (LCons (PrimeRule j 1 ((Refl (PVar 0))) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst j) (idx 0))
     (idx 0)
     (DeclConst A)))
!(R j-refl-fault
   (tc:compare 40 skip-j-refl
     (LCons (PrimeRule j 1 ((Refl (PVar 0))) (PVar 0)) LNil)
     Nil Nil
     (App (DeclConst j) (Refl (DeclConst a)))
     (DeclConst a)
     (DeclConst A)))
