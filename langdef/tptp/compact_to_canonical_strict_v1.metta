; The forms of the canonical terms of compact values that belong to the strict
; grammar: a disjunction is the list of its literals, and introduced(T, I, P)
; carries its parents.
(gslt-presentation-v1 TptpCanonicalFromCompactStrictV1
  (signature
    (operator LCons 2)
    (operator bnf 2)
    (operator bnf 4)
    (operator metta-equation 2)
    (operator tptp-canonical:disjunction 1)
    (operator tptp-canonical:introduced 1)
    (operator tptp-canonical:leaf 1)
    (operator tptp-canonical:parents 1)
    (operator tptp-canonical:useful-info 1)
    (operator tptp-compact:list-value 1))
  (equations)
  (rewrites
    (rule disjunction (head (metta-equation (tptp-canonical:disjunction ?literals) (bnf cnf_disjunction (tptp-compact:list-value ?literals)))) (body))
    (rule introduced
      (head (metta-equation (tptp-canonical:introduced (LCons ?type (LCons ?info (LCons ?parents LNil))))
                            (bnf internal_source (tptp-canonical:leaf ?type) (tptp-canonical:useful-info ?info) (tptp-canonical:parents ?parents))))
      (body))
  ))
