(= (nik-status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))

(= (nik-receipt $label
      (PrimeVerdict Established $judgment
        (NIKReceiptV1
          (NIKAuthorityV1 $authority $system $revision $digest)
          (Catalog $catalog)
          (Outcome accepted)
          $realizations
          (Agreement not-run)
          (TotalWork $work)
          (Diagnostic None))))
   ($label $authority $system $revision True))

(= (nik-package
      (PrimeDefV2 $identity $language $contexts
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

(= (nik-unsupported-receipt $label
      (PrimeVerdict Undetermined $judgment
        (NIKReceiptV1
          $authority
          (Catalog $catalog)
          (Outcome unsupported)
          (Realizations
            (NativeReplay not-run False 0)
            (HornReference not-run False 0)
            (CompiledWorklist not-run False 0))
          (Agreement not-run)
          (TotalWork 0)
          $diagnostic)))
   ($label $authority False))

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

(= (nik-check $authority $goal-computation $proof-computation)
   (let $goal $goal-computation
     (let $proof $proof-computation
       (prime-judge &self (Check $authority $goal $proof)))))
(= (nik-check-bounded
      $authority $goal-computation $proof-computation $budget)
   (let $goal $goal-computation
     (let $proof $proof-computation
       (prime-judge &self (Check $authority $goal $proof) $budget))))
(= (dtt-check)
   (nik-check DTT (dtt-goal) (dtt-proof)))
(= (hotg-check)
   (nik-check HOTG (hotg-goal) (hotg-proof)))
(= (megalodon-imp-check)
   (nik-check MEGALODON-IMP
     (megalodon-imp-goal) (megalodon-imp-proof)))

!(nik-package (prime-package &self))
!(nik-receipt DTTReceipt (dtt-check))
!(nik-receipt HOTGReceipt (hotg-check))
!(nik-receipt MegalodonImpReceipt (megalodon-imp-check))
!(nik-status DTTAccepts (dtt-check))
!(nik-status HOTGAccepts (hotg-check))
!(nik-status MegalodonImpAccepts (megalodon-imp-check))
!(nik-status DTTRejectsHOTGProof
   (nik-check DTT (dtt-goal) (hotg-proof)))
!(nik-status HOTGRejectsDTTProof
   (nik-check HOTG (hotg-goal) (dtt-proof)))
!(nik-status UnknownAuthority
   (nik-check UnknownAuthority (dtt-goal) (dtt-proof)))
!(nik-unsupported-receipt UnknownAuthorityReceipt
   (nik-check UnknownAuthority (dtt-goal) (dtt-proof)))
!(nik-status MalformedArticle
   (nik-check DTT (dtt-goal)
     (GProof (GRuleInst "z" LNil) BrokenChildren)))
!(nik-status SemanticBoundTooSmall
   (nik-check-bounded MEGALODON-IMP
     (megalodon-imp-goal) (megalodon-imp-proof) 1))
!(nik-status SemanticBoundExact
   (nik-check-bounded MEGALODON-IMP
     (megalodon-imp-goal) (megalodon-imp-proof) 2))
!(nik-status ExistingDependentCheck
   (prime-judge &self (Check 1 Number)))
!(+ 1 2)
