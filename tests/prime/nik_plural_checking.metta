(= (nik-package
      (PrimeDefV3 $identity $language $contexts
        (CheckingV1
          (Gradual $gradual)
          (AuthorityIndexedJudgment $judgment)
          (NIKAuthorityCatalogV1 $catalog
            (NIKAuthorityV1 $first $first-system $first-revision $first-digest)
            (NIKAuthorityV1 $second $second-system $second-revision $second-digest)
            (NIKAuthorityV1 $third $third-system $third-revision $third-digest)
            (NIKAuthorityV1 $fourth $fourth-system $fourth-revision $fourth-digest))
          $local)
        $results $effects $evidence))
   (PrimeChecking $gradual $judgment $first $second $third $fourth))

(= (dtt-goal)
   (PApp "T"
     (LCons (PApp "C" LNil)
       (LCons (PApp "Z" LNil)
         (LCons (PApp "N" LNil) LNil)))))
(= (dtt-proof)
   (GProof (GRuleInst "z" LNil) PrNil))

(= (hotg-goal)
   (PApp "H"
     (LCons
       (PApp "I"
         (LCons (PApp "n" LNil)
           (LCons (PApp "U" (LCons (PApp "n" LNil) LNil)) LNil)))
       LNil)))
(= (hotg-proof)
   (GProof
     (GRuleInst "q" LNil)
     PrNil))

(= (megalodon-imp-goal)
   (PApp "MProves"
     (LCons (PApp "MCtxNil" LNil)
       (LCons
         (PApp "MImp"
           (LCons (PApp "MAtom" (LCons (PApp "MZero" LNil) LNil))
             (LCons
               (PApp "MAtom" (LCons (PApp "MZero" LNil) LNil))
               LNil)))
         LNil))))
(= (megalodon-imp-proof)
   (GProof
     (GRuleInst "megalodon-imp-intro"
       (LCons (PApp "MCtxNil" LNil)
         (LCons
           (PApp "MAtom" (LCons (PApp "MZero" LNil) LNil))
           (LCons
             (PApp "MAtom" (LCons (PApp "MZero" LNil) LNil))
             LNil))))
     (PrCons
       (GProof
         (GRuleInst "megalodon-hyp-zero"
           (LCons (PApp "MCtxNil" LNil)
             (LCons
               (PApp "MAtom" (LCons (PApp "MZero" LNil) LNil))
               LNil)))
         PrNil)
       PrNil)))

(= (check-generated-proof $authority $goal-computation $proof-computation)
   (let $goal $goal-computation
     (let $proof $proof-computation
       (nik:check $authority $goal $proof))))
(= (check-generated-proof-bounded
      $authority $goal-computation $proof-computation $budget)
   (let $goal $goal-computation
     (let $proof $proof-computation
       (nik:check $authority $goal $proof $budget))))
(= (dtt-check)
   (check-generated-proof DTT (dtt-goal) (dtt-proof)))
(= (hotg-check)
   (check-generated-proof HOTG (hotg-goal) (hotg-proof)))
(= (megalodon-imp-check)
   (check-generated-proof MEGALODON-IMP
     (megalodon-imp-goal) (megalodon-imp-proof)))

!(nik-package (prime-package &self))
!(dtt-check)
!(hotg-check)
!(megalodon-imp-check)
!(check-generated-proof DTT (dtt-goal) (hotg-proof))
!(check-generated-proof HOTG (hotg-goal) (dtt-proof))
!(check-generated-proof UnknownAuthority (dtt-goal) (dtt-proof))
!(check-generated-proof DTT (dtt-goal)
   (GProof (GRuleInst "z" LNil) BrokenChildren))
!(check-generated-proof-bounded MEGALODON-IMP
   (megalodon-imp-goal) (megalodon-imp-proof) 1)
!(check-generated-proof-bounded MEGALODON-IMP
   (megalodon-imp-goal) (megalodon-imp-proof) 2)
!(type:check 1 Number)
!(+ 1 2)
