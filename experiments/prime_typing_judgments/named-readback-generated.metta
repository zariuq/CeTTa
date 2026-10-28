; Generated from ContextualJudgmentExamples; exact named and native syntax comparisons.
(hol:profile base)
!(import! &self judgments.metta)
!(assertEqual (abstract-term ((: A (u 0)) (: x A)) x) (quote (lam (A : (u 0)) (lam (x : A) x))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A)) A) (quote (-> (A : (u 0)) (-> (x : A) A))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) x)) (-> (A : (u 0)) (-> (x : A) A))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) x)) (-> (A : (u 0)) (-> (x : A) A))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (idx 0))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (idx 1)))))))
!(assertEqual (abstract-term ((: A (u 0)) (: x A) (: y A)) x) (quote (lam (A : (u 0)) (lam (x : A) (lam (y : A) x)))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A) (: y A)) A) (quote (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) (lam (y : A) x))) (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) (lam (y : A) x))) (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (Lam (idx 1) (idx 1)))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (Pi (idx 1) (idx 2))))))))
!(assertEqual (abstract-term ((: A (u 0)) (: x A) (: y A)) y) (quote (lam (A : (u 0)) (lam (x : A) (lam (y : A) y)))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A) (: y A)) A) (quote (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) (lam (y : A) y))) (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) (lam (y : A) y))) (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (Lam (idx 1) (idx 0)))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (Pi (idx 1) (idx 2))))))))
!(assertEqual (abstract-term ((: A (u 0)) (: x A)) (lam (y : A) x)) (quote (lam (A : (u 0)) (lam (x : A) (lam (y : A) x)))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A)) (-> (y : A) A)) (quote (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) (lam (y : A) x))) (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) (lam (y : A) x))) (-> (A : (u 0)) (-> (x : A) (-> (y : A) A)))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (Lam (idx 1) (idx 1)))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (Pi (idx 1) (idx 2))))))))
!(assertEqual (abstract-term ((: A (u 0)) (: x A)) (pair x (refl x))) (quote (lam (A : (u 0)) (lam (x : A) (pair x (refl x))))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A)) (sigma (y : A) (id A y x))) (quote (-> (A : (u 0)) (-> (x : A) (sigma (y : A) (id A y x))))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) (pair x (refl x)))) (-> (A : (u 0)) (-> (x : A) (sigma (y : A) (id A y x))))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) (pair x (refl x)))) (-> (A : (u 0)) (-> (x : A) (sigma (y : A) (id A y x))))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (Pair (idx 0) (Refl (idx 0))))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (Sigma (idx 1) (Id (idx 2) (idx 0) (idx 1)))))))))
!(assertEqual (abstract-term ((: A (u 0)) (: x A) (: p (sigma (y : A) (id A y x)))) (fst p)) (quote (lam (A : (u 0)) (lam (x : A) (lam (p : (sigma (y : A) (id A y x))) (fst p))))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A) (: p (sigma (y : A) (id A y x)))) A) (quote (-> (A : (u 0)) (-> (x : A) (-> (p : (sigma (y : A) (id A y x))) A)))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) (lam (p : (sigma (y : A) (id A y x))) (fst p)))) (-> (A : (u 0)) (-> (x : A) (-> (p : (sigma (y : A) (id A y x))) A)))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) (lam (p : (sigma (y : A) (id A y x))) (fst p)))) (-> (A : (u 0)) (-> (x : A) (-> (p : (sigma (y : A) (id A y x))) A)))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (Lam (Sigma (idx 1) (Id (idx 2) (idx 0) (idx 1))) (Fst (idx 0))))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (Pi (Sigma (idx 1) (Id (idx 2) (idx 0) (idx 1))) (idx 2))))))))
!(assertEqual (abstract-term ((: A (u 0)) (: x A) (: p (sigma (y : A) (id A y x)))) (snd p)) (quote (lam (A : (u 0)) (lam (x : A) (lam (p : (sigma (y : A) (id A y x))) (snd p))))))
!(assertEqual (abstract-type ((: A (u 0)) (: x A) (: p (sigma (y : A) (id A y x)))) (id A (fst p) x)) (quote (-> (A : (u 0)) (-> (x : A) (-> (p : (sigma (y : A) (id A y x))) (id A (fst p) x))))))
!(assertEqual (type:check (lam (A : (u 0)) (lam (x : A) (lam (p : (sigma (y : A) (id A y x))) (snd p)))) (-> (A : (u 0)) (-> (x : A) (-> (p : (sigma (y : A) (id A y x))) (id A (fst p) x))))) True)
!(let (PrimeKernelQueryV1 $judgment $route $context $levels $rules
        (Subject $subject) $subject-type (Object $object) $object-type)
      (type:kernel-query (type:check (lam (A : (u 0)) (lam (x : A) (lam (p : (sigma (y : A) (id A y x))) (snd p)))) (-> (A : (u 0)) (-> (x : A) (-> (p : (sigma (y : A) (id A y x))) (id A (fst p) x))))))
   (assertEqual (quote ($subject $object)){$subject $object}
     (quote ((Lam (Sort (LevelConst 0)) (Lam (idx 0) (Lam (Sigma (idx 1) (Id (idx 2) (idx 0) (idx 1))) (Snd (idx 0))))) (Pi (Sort (LevelConst 0)) (Pi (idx 0) (Pi (Sigma (idx 1) (Id (idx 2) (idx 0) (idx 1))) (Id (idx 2) (Fst (idx 0)) (idx 1)))))))))
!(assertEqual (judge (quote (|- ((: A (u 0)) (: x A) (: x A)) (: x A))))
   (Unsupported duplicate-display-name))
!(assertEqual (judge (quote (|- ((: A (u 0)) (: x A)) (: x (u 0))))) False)
!(assertEqual (judge (quote (|- ((: A (u 0)) (: () A)) (: A (u 0)))))
   (Unsupported non-symbol-binder))
