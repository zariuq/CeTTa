; Authored sequence-assertion declarations elaborate to ordinary structural
; proof rules.  Floating hypotheses bind sequence images, essential
; hypotheses carry explicit instantiation evidence, and support obligations
; remain ordinary premises checked by the same chronological article checker.

(gslt-presentation-v1 ProofGSLTSequenceSchemaV1
  (signature
    (operator source-proof-sequence-schema-layer 3)
    (operator source-proof-sequence-schema 8)
    (operator proof-sequence-schema-variable-v1 3)
    (operator proof-sequence-schema-essential-v1 2)
    (operator proof-sequence-schema-disjoint-v1 2)
    (operator proof-sequence-schema-environment 4)
    (operator proof-sequence-schema-variable-formals 3)
    (operator proof-sequence-schema-essential-formals 3)
    (operator proof-sequence-schema-variable-premises 6)
    (operator proof-sequence-schema-essential-premises 6)
    (operator proof-sequence-schema-variable-image 3)
    (operator proof-sequence-schema-disjoint-premises 5)
  )
  (equations)
  (rewrites
    (rule proof-sequence-schema-environment-nil
      (head
        (proof-sequence-schema-environment
          ?owner ?base proof-list-nil-v1
          (proof-apply-v1 ?environment-nil proof-list-nil-v1)))
      (body
        (source-proof-sequence-constructor-role
          ?owner ?base environment-nil-v1 ?environment-nil)))
    (rule proof-sequence-schema-environment-cons
      (head
        (proof-sequence-schema-environment
          ?owner ?base
          (proof-list-cons-v1
            (proof-sequence-schema-variable-v1
              ?variable ?image-formal ?typecode)
            ?rest)
          (proof-apply-v1 ?environment-cons
            (proof-list-cons-v1
              ?variable
              (proof-list-cons-v1
                (proof-fvar-v1 ?image-formal)
                (proof-list-cons-v1
                  ?rest-environment proof-list-nil-v1))))))
      (body
        (source-proof-sequence-constructor-role
          ?owner ?base environment-cons-v1 ?environment-cons)
        (proof-sequence-schema-environment
          ?owner ?base ?rest ?rest-environment)))

    ; These list builders use their second argument as the already-built tail.
    ; This preserves declaration order without a separate append authority.
    (rule proof-sequence-schema-variable-formals-nil
      (head
        (proof-sequence-schema-variable-formals
          proof-list-nil-v1 ?tail ?tail))
      (body))
    (rule proof-sequence-schema-variable-formals-cons
      (head
        (proof-sequence-schema-variable-formals
          (proof-list-cons-v1
            (proof-sequence-schema-variable-v1
              ?variable ?image-formal ?typecode)
            ?rest)
          ?tail
          (proof-list-cons-v1
            (proof-formal-v1 ?image-formal proof-zero-v1)
            ?out-rest)))
      (body
        (proof-sequence-schema-variable-formals
          ?rest ?tail ?out-rest)))

    (rule proof-sequence-schema-essential-formals-nil
      (head
        (proof-sequence-schema-essential-formals
          proof-list-nil-v1 ?tail ?tail))
      (body))
    (rule proof-sequence-schema-essential-formals-cons
      (head
        (proof-sequence-schema-essential-formals
          (proof-list-cons-v1
            (proof-sequence-schema-essential-v1
              ?actual-formal ?template)
            ?rest)
          ?tail
          (proof-list-cons-v1
            (proof-formal-v1 ?actual-formal proof-zero-v1)
            ?out-rest)))
      (body
        (proof-sequence-schema-essential-formals
          ?rest ?tail ?out-rest)))

    (rule proof-sequence-schema-variable-premises-nil
      (head
        (proof-sequence-schema-variable-premises
          ?owner ?base proof-list-nil-v1 ?provable ?tail ?tail))
      (body))
    (rule proof-sequence-schema-variable-premises-cons
      (head
        (proof-sequence-schema-variable-premises
          ?owner ?base
          (proof-list-cons-v1
            (proof-sequence-schema-variable-v1
              ?variable ?image-formal ?typecode)
            ?rest)
          ?provable ?tail
          (proof-list-cons-v1
            (proof-apply-v1 ?provable
              (proof-list-cons-v1
                (proof-apply-v1 ?sequence-cons
                  (proof-list-cons-v1
                    ?typecode
                    (proof-list-cons-v1
                      (proof-fvar-v1 ?image-formal)
                      proof-list-nil-v1)))
                proof-list-nil-v1))
            ?out-rest)))
      (body
        (source-proof-sequence-constructor-role
          ?owner ?base sequence-cons-v1 ?sequence-cons)
        (proof-sequence-schema-variable-premises
          ?owner ?base ?rest ?provable ?tail ?out-rest)))

    (rule proof-sequence-schema-essential-premises-nil
      (head
        (proof-sequence-schema-essential-premises
          proof-list-nil-v1 ?provable ?instantiate
          ?environment ?tail ?tail))
      (body))
    (rule proof-sequence-schema-essential-premises-cons
      (head
        (proof-sequence-schema-essential-premises
          (proof-list-cons-v1
            (proof-sequence-schema-essential-v1
              ?actual-formal ?template)
            ?rest)
          ?provable ?instantiate ?environment ?tail
          (proof-list-cons-v1
            (proof-apply-v1 ?provable
              (proof-list-cons-v1
                (proof-fvar-v1 ?actual-formal)
                proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ?instantiate
                (proof-list-cons-v1
                  ?template
                  (proof-list-cons-v1
                    ?environment
                    (proof-list-cons-v1
                      (proof-fvar-v1 ?actual-formal)
                      proof-list-nil-v1))))
              ?out-rest))))
      (body
        (proof-sequence-schema-essential-premises
          ?rest ?provable ?instantiate ?environment ?tail ?out-rest)))

    (rule proof-sequence-schema-variable-image-head
      (head
        (proof-sequence-schema-variable-image
          (proof-list-cons-v1
            (proof-sequence-schema-variable-v1
              ?variable ?image-formal ?typecode)
            ?rest)
          ?variable ?image-formal))
      (body))
    (rule proof-sequence-schema-variable-image-tail
      (head
        (proof-sequence-schema-variable-image
          (proof-list-cons-v1 ?entry ?rest)
          ?variable ?image-formal))
      (body
        (proof-sequence-schema-variable-image
          ?rest ?variable ?image-formal)))

    (rule proof-sequence-schema-disjoint-premises-nil
      (head
        (proof-sequence-schema-disjoint-premises
          ?variables proof-list-nil-v1 ?support-apart ?tail ?tail))
      (body))
    (rule proof-sequence-schema-disjoint-premises-cons
      (head
        (proof-sequence-schema-disjoint-premises
          ?variables
          (proof-list-cons-v1
            (proof-sequence-schema-disjoint-v1 ?left ?right)
            ?rest)
          ?support-apart ?tail
          (proof-list-cons-v1
            (proof-apply-v1 ?support-apart
              (proof-list-cons-v1
                (proof-fvar-v1 ?left-image)
                (proof-list-cons-v1
                  (proof-fvar-v1 ?right-image)
                  proof-list-nil-v1)))
            ?out-rest)))
      (body
        (proof-sequence-schema-variable-image
          ?variables ?left ?left-image)
        (proof-sequence-schema-variable-image
          ?variables ?right ?right-image)
        (proof-sequence-schema-disjoint-premises
          ?variables ?rest ?support-apart ?tail ?out-rest)))

    (rule proof-sequence-schema-elaborate
      (head
        (source-proof-rule
          ?owner ?rule-id ?formals ?premises
          (proof-apply-v1 ?provable
            (proof-list-cons-v1
              (proof-apply-v1 ?sequence-cons
                (proof-list-cons-v1
                  ?conclusion-type
                  (proof-list-cons-v1
                    (proof-fvar-v1 ?result-formal)
                    proof-list-nil-v1)))
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-schema-layer
          ?owner ?base ?provable)
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-extension ?owner ?base)
        (source-proof-judgment
          ?owner ?provable (proof-succ-v1 proof-zero-v1))
        (source-proof-sequence-constructor-role
          ?owner ?base sequence-cons-v1 ?sequence-cons)
        (source-proof-sequence-judgment-role
          ?owner instantiate-v1 ?instantiate)
        (source-proof-sequence-judgment-role
          ?owner support-apart-v1 ?support-apart)
        (source-proof-sequence-schema
          ?owner ?rule-id ?variables ?essentials
          ?conclusion-type ?conclusion-template
          ?result-formal ?disjoints)
        (proof-sequence-schema-environment
          ?owner ?base ?variables ?environment)
        (proof-sequence-schema-essential-formals
          ?essentials
          (proof-list-cons-v1
            (proof-formal-v1 ?result-formal proof-zero-v1)
            proof-list-nil-v1)
          ?essential-formals)
        (proof-sequence-schema-variable-formals
          ?variables ?essential-formals ?formals)
        (proof-sequence-schema-disjoint-premises
          ?variables ?disjoints ?support-apart
          proof-list-nil-v1 ?disjoint-premises)
        (proof-sequence-schema-essential-premises
          ?essentials ?provable ?instantiate ?environment
          (proof-list-cons-v1
            (proof-apply-v1 ?instantiate
              (proof-list-cons-v1
                ?conclusion-template
                (proof-list-cons-v1
                  ?environment
                  (proof-list-cons-v1
                    (proof-fvar-v1 ?result-formal)
                    proof-list-nil-v1))))
            ?disjoint-premises)
          ?essential-premises)
        (proof-sequence-schema-variable-premises
          ?owner ?base ?variables ?provable
          ?essential-premises ?premises)))
  ))
