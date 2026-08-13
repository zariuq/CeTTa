(gslt-presentation-v1 prime-typed-publication-core-v1
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber prime)
    (outcome-algebra (PEstablished PRefuted PUndetermined)
      (exclusive)
      (default PUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default))
  (signature
    (operator PList 1)
    (operator PContracts 2)
    (operator PrimeContractMatches 2)
    (operator PrimeContractDisjoint 2)
    (operator PrimeAnyContractMatches 2)
    (operator PrimeAllContractsDisjoint 2)
    (operator PrimePublicationJudges 3)
  )
  (equations)
  (rewrites
    (rule prime-publication-contract-number-exact
      (nik-rule-frame-v1
        (native-projection native-contract-matching)
        (role calculus))
      (head (PrimeContractMatches PNumber PNumber))
      (body))
    (rule prime-publication-contract-string-exact
      (nik-rule-frame-v1
        (native-projection native-contract-matching)
        (role calculus))
      (head (PrimeContractMatches PString PString))
      (body))
    (rule prime-publication-contract-bool-exact
      (nik-rule-frame-v1
        (native-projection native-contract-matching)
        (role calculus))
      (head (PrimeContractMatches PBool PBool))
      (body))
    (rule prime-publication-contract-list-structural
      (nik-rule-frame-v1
        (native-projection native-contract-matching)
        (role calculus))
      (head (PrimeContractMatches (PList ?actual) (PList ?expected)))
      (body (PrimeContractMatches ?actual ?expected)))
    (rule prime-publication-contract-dynamic
      (nik-rule-frame-v1
        (native-projection native-contract-matching)
        (role calculus))
      (head (PrimeContractMatches ?actual PDynamic))
      (body))
    (rule prime-publication-contract-atom-top
      (nik-rule-frame-v1
        (native-projection native-contract-matching)
        (role calculus))
      (head (PrimeContractMatches ?actual PAtom))
      (body))
    (rule prime-publication-contract-number-string-disjoint
      (nik-rule-frame-v1
        (native-projection native-contract-disjointness)
        (role calculus))
      (head (PrimeContractDisjoint PNumber PString))
      (body))
    (rule prime-publication-contract-number-bool-disjoint
      (nik-rule-frame-v1
        (native-projection native-contract-disjointness)
        (role calculus))
      (head (PrimeContractDisjoint PNumber PBool))
      (body))
    (rule prime-publication-contract-string-number-disjoint
      (nik-rule-frame-v1
        (native-projection native-contract-disjointness)
        (role calculus))
      (head (PrimeContractDisjoint PString PNumber))
      (body))
    (rule prime-publication-contract-string-bool-disjoint
      (nik-rule-frame-v1
        (native-projection native-contract-disjointness)
        (role calculus))
      (head (PrimeContractDisjoint PString PBool))
      (body))
    (rule prime-publication-contract-bool-number-disjoint
      (nik-rule-frame-v1
        (native-projection native-contract-disjointness)
        (role calculus))
      (head (PrimeContractDisjoint PBool PNumber))
      (body))
    (rule prime-publication-contract-bool-string-disjoint
      (nik-rule-frame-v1
        (native-projection native-contract-disjointness)
        (role calculus))
      (head (PrimeContractDisjoint PBool PString))
      (body))
    (rule prime-publication-any-contract-head
      (nik-rule-frame-v1
        (native-projection native-contract-existence)
        (role calculus))
      (head (PrimeAnyContractMatches ?actual (PContracts ?head ?tail)))
      (body (PrimeContractMatches ?actual ?head)))
    (rule prime-publication-any-contract-tail
      (nik-rule-frame-v1
        (native-projection native-contract-existence)
        (role calculus))
      (head (PrimeAnyContractMatches ?actual (PContracts ?head ?tail)))
      (body (PrimeAnyContractMatches ?actual ?tail)))
    (rule prime-publication-all-contracts-single
      (nik-rule-frame-v1
        (native-projection native-contract-refutation)
        (role calculus))
      (head (PrimeAllContractsDisjoint ?actual
              (PContracts ?contract PNoContracts)))
      (body (PrimeContractDisjoint ?actual ?contract)))
    (rule prime-publication-all-contracts-cons
      (nik-rule-frame-v1
        (native-projection native-contract-refutation)
        (role calculus))
      (head (PrimeAllContractsDisjoint ?actual
              (PContracts ?head ?tail)))
      (body
        (PrimeContractDisjoint ?actual ?head)
        (PrimeAllContractsDisjoint ?actual ?tail)))
    (rule prime-publication-publication-established
      (nik-rule-frame-v1
        (native-projection native-typed-publication)
        (role calculus))
      (head (PrimePublicationJudges ?actual ?contracts PEstablished))
      (body (PrimeAnyContractMatches ?actual ?contracts)))
    (rule prime-publication-publication-refuted
      (nik-rule-frame-v1
        (native-projection native-typed-publication)
        (role calculus))
      (head (PrimePublicationJudges ?actual ?contracts PRefuted))
      (body (PrimeAllContractsDisjoint ?actual ?contracts)))
  ))
