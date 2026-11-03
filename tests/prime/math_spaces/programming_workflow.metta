!(import! &self math_space_store)
!(bind! &maps (math:new "list-programming"))
!(math:declare &maps (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
!(math:declare &maps (quote (hol:inductive NList (u 0) (: Nil NList) (: Cons (-> Nat NList NList)))))
!(math:declare &maps (quote (hol:define map (-> (-> Nat Nat) NList NList)
  (= (map $f Nil) Nil) (= (map $f (Cons $x $xs)) (Cons ($f $x) (map $f $xs))))))
!(math:declare &maps (quote (hol:define comp (-> (-> Nat Nat) (-> Nat Nat) Nat Nat)
  (= (comp $f $g $x) ($f ($g $x))))))
!(math:declare &maps (quote (hol:define double (-> Nat Nat)
  (= (double Z) Z) (= (double (S $n)) (S (S (double $n)))))))
!(assertEqual (math:run &maps (quote (map S (map double (Cons (S Z) Nil)))))
   (Cons (S (S (S Z))) Nil))
!(math:goal &maps "fusion" type
  (quote (-> (f : (-> Nat Nat)) (g : (-> Nat Nat)) (xs : NList)
    (id NList (map f (map g xs)) (map (comp f g) xs)))) "Mapping twice agrees with composed mapping.")
!(chain (math:attempt &maps "too-simple" "fusion" "reflexivity"
   (quote (lam f (lam g (lam xs (refl (map f (map g xs))))))) 1000000) $checked
   (assertEqual
     (case (math:attempt-status &maps "too-simple")
       (((rejected $why) RejectedEvidence) ($other Wrong))) RejectedEvidence))
(= (map-fusion-strategy)
   (quote (lam f (lam g (lam xs
     (NList-rec (lam l (id NList (map f (map g l)) (map (comp f g) l)))
       (refl Nil)
       (lam x (lam l (lam ih
         (id:eliminate NList (map f (map g l))
           (lam y (lam e (id NList (Cons (f (g x)) (map f (map g l))) (Cons (f (g x)) y))))
           (refl (Cons (f (g x)) (map f (map g l)))) (map (comp f g) l) ih)))) xs))))))
!(math:propose &maps "induction" "fusion" "list-induction" (map-fusion-strategy))
!(chain (math:check &maps "induction" 1000000) $checked
   (assertEqual (math:attempt-status &maps "induction") checked))
!(assertEqual (math:publish &maps "induction" map-fusion 1000000)
   (Math:Published map-fusion "induction"))
!(assertEqual
   (math:run &maps (quote (map S (map double (Cons (S Z) Nil)))))
   (math:run &maps (quote (map (comp S double) (Cons (S Z) Nil)))))
!(assertEqual
   (== (math:run &maps (quote (map S (map double (Cons (S Z) Nil)))))
       (math:run &maps (quote (map (comp double S) (Cons (S Z) Nil))))) False)
!(let $snapshot (math:serialize &maps)
   (assertEqual
     (case (math:restore $snapshot 1000000)
       (((Math:Restored $restored)
          (case (math:goal-status $restored "fusion" 1000000)
            (((Math:GoalStatus "fusion" checked $evidence)
               (math:run $restored (quote (map (comp S double) (Cons (S Z) Nil)))))
             ($other Wrong))))
        ($other Wrong))) (Cons (S (S (S Z))) Nil)))
