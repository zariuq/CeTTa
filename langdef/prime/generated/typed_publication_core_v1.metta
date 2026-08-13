(gslt-presentation-v1 prime-typed-publication-core-v1
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
      (head (PrimeContractMatches PNumber PNumber))
      (body))
    (rule prime-publication-contract-string-exact
      (head (PrimeContractMatches PString PString))
      (body))
    (rule prime-publication-contract-bool-exact
      (head (PrimeContractMatches PBool PBool))
      (body))
    (rule prime-publication-contract-list-structural
      (head (PrimeContractMatches (PList ?actual) (PList ?expected)))
      (body (PrimeContractMatches ?actual ?expected)))
    (rule prime-publication-contract-dynamic
      (head (PrimeContractMatches ?actual PDynamic))
      (body))
    (rule prime-publication-contract-atom-top
      (head (PrimeContractMatches ?actual PAtom))
      (body))
    (rule prime-publication-contract-number-string-disjoint
      (head (PrimeContractDisjoint PNumber PString))
      (body))
    (rule prime-publication-contract-number-bool-disjoint
      (head (PrimeContractDisjoint PNumber PBool))
      (body))
    (rule prime-publication-contract-string-number-disjoint
      (head (PrimeContractDisjoint PString PNumber))
      (body))
    (rule prime-publication-contract-string-bool-disjoint
      (head (PrimeContractDisjoint PString PBool))
      (body))
    (rule prime-publication-contract-bool-number-disjoint
      (head (PrimeContractDisjoint PBool PNumber))
      (body))
    (rule prime-publication-contract-bool-string-disjoint
      (head (PrimeContractDisjoint PBool PString))
      (body))
    (rule prime-publication-any-contract-head
      (head (PrimeAnyContractMatches ?actual (PContracts ?head ?tail)))
      (body (PrimeContractMatches ?actual ?head)))
    (rule prime-publication-any-contract-tail
      (head (PrimeAnyContractMatches ?actual (PContracts ?head ?tail)))
      (body (PrimeAnyContractMatches ?actual ?tail)))
    (rule prime-publication-all-contracts-single
      (head (PrimeAllContractsDisjoint ?actual
              (PContracts ?contract PNoContracts)))
      (body (PrimeContractDisjoint ?actual ?contract)))
    (rule prime-publication-all-contracts-cons
      (head (PrimeAllContractsDisjoint ?actual
              (PContracts ?head ?tail)))
      (body
        (PrimeContractDisjoint ?actual ?head)
        (PrimeAllContractsDisjoint ?actual ?tail)))
    (rule prime-publication-publication-established
      (head (PrimePublicationJudges ?actual ?contracts PEstablished))
      (body (PrimeAnyContractMatches ?actual ?contracts)))
    (rule prime-publication-publication-refuted
      (head (PrimePublicationJudges ?actual ?contracts PRefuted))
      (body (PrimeAllContractsDisjoint ?actual ?contracts)))
  ))
