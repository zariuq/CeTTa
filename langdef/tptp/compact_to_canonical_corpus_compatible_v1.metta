; The forms of the canonical terms of compact values that belong to the
; corpus-compatible grammar: a disjunction is the chain of its literals,
; introduced may omit its parents, and a parameter of an NHF connective may be
; a bare term, which is neither an index (#t) nor a definition.
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
    (operator tptp-canonical:parameter 5)
    (operator tptp-canonical:parents 1)
    (operator tptp-canonical:tff-form 3)
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
    ; A bare NHF parameter is a TFF term of its key, which is any key but hash
    ; and definition; it takes the TFF form of that key.
    (rule bare-parameter-and (head (metta-equation (tptp-canonical:parameter thf and ?view ?value ?parameter) (tptp-canonical:tff-form and ?view ?value))) (body))
    (rule bare-parameter-apply (head (metta-equation (tptp-canonical:parameter thf apply ?view ?value ?parameter) (tptp-canonical:tff-form apply ?view ?value))) (body))
    (rule bare-parameter-assignment (head (metta-equation (tptp-canonical:parameter thf assignment ?view ?value ?parameter) (tptp-canonical:tff-form assignment ?view ?value))) (body))
    (rule bare-parameter-data-cnf (head (metta-equation (tptp-canonical:parameter thf data-cnf ?view ?value ?parameter) (tptp-canonical:tff-form data-cnf ?view ?value))) (body))
    (rule bare-parameter-data-fof (head (metta-equation (tptp-canonical:parameter thf data-fof ?view ?value ?parameter) (tptp-canonical:tff-form data-fof ?view ?value))) (body))
    (rule bare-parameter-data-fot (head (metta-equation (tptp-canonical:parameter thf data-fot ?view ?value ?parameter) (tptp-canonical:tff-form data-fot ?view ?value))) (body))
    (rule bare-parameter-data-tff (head (metta-equation (tptp-canonical:parameter thf data-tff ?view ?value ?parameter) (tptp-canonical:tff-form data-tff ?view ?value))) (body))
    (rule bare-parameter-data-thf (head (metta-equation (tptp-canonical:parameter thf data-thf ?view ?value ?parameter) (tptp-canonical:tff-form data-thf ?view ?value))) (body))
    (rule bare-parameter-equal (head (metta-equation (tptp-canonical:parameter thf equal ?view ?value ?parameter) (tptp-canonical:tff-form equal ?view ?value))) (body))
    (rule bare-parameter-leaf (head (metta-equation (tptp-canonical:parameter thf leaf ?view ?value ?parameter) (tptp-canonical:tff-form leaf ?view ?value))) (body))
    (rule bare-parameter-let (head (metta-equation (tptp-canonical:parameter thf let ?view ?value ?parameter) (tptp-canonical:tff-form let ?view ?value))) (body))
    (rule bare-parameter-long-connective (head (metta-equation (tptp-canonical:parameter thf long-connective ?view ?value ?parameter) (tptp-canonical:tff-form long-connective ?view ?value))) (body))
    (rule bare-parameter-mapping (head (metta-equation (tptp-canonical:parameter thf mapping ?view ?value ?parameter) (tptp-canonical:tff-form mapping ?view ?value))) (body))
    (rule bare-parameter-nonassoc (head (metta-equation (tptp-canonical:parameter thf nonassoc ?view ?value ?parameter) (tptp-canonical:tff-form nonassoc ?view ?value))) (body))
    (rule bare-parameter-or (head (metta-equation (tptp-canonical:parameter thf or ?view ?value ?parameter) (tptp-canonical:tff-form or ?view ?value))) (body))
    (rule bare-parameter-prefix (head (metta-equation (tptp-canonical:parameter thf prefix ?view ?value ?parameter) (tptp-canonical:tff-form prefix ?view ?value))) (body))
    (rule bare-parameter-product (head (metta-equation (tptp-canonical:parameter thf product ?view ?value ?parameter) (tptp-canonical:tff-form product ?view ?value))) (body))
    (rule bare-parameter-quantifier (head (metta-equation (tptp-canonical:parameter thf quantifier ?view ?value ?parameter) (tptp-canonical:tff-form quantifier ?view ?value))) (body))
    (rule bare-parameter-sequent (head (metta-equation (tptp-canonical:parameter thf sequent ?view ?value ?parameter) (tptp-canonical:tff-form sequent ?view ?value))) (body))
    (rule bare-parameter-subtype (head (metta-equation (tptp-canonical:parameter thf subtype ?view ?value ?parameter) (tptp-canonical:tff-form subtype ?view ?value))) (body))
    (rule bare-parameter-tuple (head (metta-equation (tptp-canonical:parameter thf tuple ?view ?value ?parameter) (tptp-canonical:tff-form tuple ?view ?value))) (body))
    (rule bare-parameter-typing (head (metta-equation (tptp-canonical:parameter thf typing ?view ?value ?parameter) (tptp-canonical:tff-form typing ?view ?value))) (body))
    (rule bare-parameter-unequal (head (metta-equation (tptp-canonical:parameter thf unequal ?view ?value ?parameter) (tptp-canonical:tff-form unequal ?view ?value))) (body))
    (rule bare-parameter-union (head (metta-equation (tptp-canonical:parameter thf union ?view ?value ?parameter) (tptp-canonical:tff-form union ?view ?value))) (body))
  ))
