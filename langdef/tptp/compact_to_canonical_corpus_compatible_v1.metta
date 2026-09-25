; The forms of the canonical terms of compact values that belong to the
; corpus-compatible grammar: a disjunction is the chain of its literals, and
; introduced may omit its parents.
(gslt-presentation-v1 TptpCanonicalFromCompactCorpusCompatibleV1
  (signature
    (operator LCons 2)
    (operator bnf 3)
    (operator bnf 4)
    (operator metta-equation 2)
    (operator tptp-canonical:chain-rest 3)
    (operator tptp-canonical:disjunction 1)
    (operator tptp-canonical:introduced 1)
    (operator tptp-canonical:leaf 1)
    (operator tptp-canonical:parents 1)
    (operator tptp-canonical:useful-info 1))
  (equations)
  (rewrites
    (rule disjunction
      (head (metta-equation (tptp-canonical:disjunction (LCons ?first ?rest))
                            (tptp-canonical:chain-rest cnf_disjunction/1 ?first ?rest)))
      (body))
    (rule introduced-parents
      (head (metta-equation (tptp-canonical:introduced (LCons ?type (LCons ?info (LCons ?parents LNil))))
                            (bnf internal_source/0 (tptp-canonical:leaf ?type) (tptp-canonical:useful-info ?info) (tptp-canonical:parents ?parents))))
      (body))
    (rule introduced-alone
      (head (metta-equation (tptp-canonical:introduced (LCons ?type (LCons ?info LNil)))
                            (bnf internal_source/1 (tptp-canonical:leaf ?type) (tptp-canonical:useful-info ?info))))
      (body))
  ))
