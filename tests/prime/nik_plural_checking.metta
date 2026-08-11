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
          (Agreement True)
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
            (NIKAuthorityV1 $second $second-system $second-revision $second-digest))
          $local)
        $results $effects $evidence))
   (PrimeChecking $gradual $judgment $first $second))

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
          (Agreement False)
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
     (GRuleInst "q" (LCons (PApp "n" LNil) LNil))
     PrNil))

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

!(nik-package (prime-package &self))
!(nik-receipt DTTReceipt (dtt-check))
!(nik-receipt HOTGReceipt (hotg-check))
!(nik-status DTTAccepts (dtt-check))
!(nik-status HOTGAccepts (hotg-check))
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
!(nik-status AggregateBoundTooSmall
   (nik-check-bounded DTT (dtt-goal) (dtt-proof) 12))
!(nik-status AggregateBoundExact
   (nik-check-bounded DTT (dtt-goal) (dtt-proof) 13))
!(nik-status ExistingDependentCheck
   (prime-judge &self (Check 1 Number)))
!(+ 1 2)
