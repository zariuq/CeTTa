; A reusable proof-theoretic presentation of finite sequence instantiation.
; Sequence splicing and support apartness are ordinary judgments with explicit
; chronological evidence.  Native accelerators may produce these articles,
; but they do not become a second semantic authority.

(gslt-presentation-v1 ProofGSLTSequenceRelationsV1
  (signature
    (operator source-proof-sequence-layer 2)
    (operator source-proof-sequence-functional-layer 2)
    (operator source-proof-sequence-relational-layer 2)
    (operator source-proof-sequence-constructor-role 4)
    (operator source-proof-sequence-judgment-role 3)
    (operator source-proof-sequence-rule-role 3)
  )
  (equations)
  (rewrites
    (rule proof-sequence-calculus-sequence-nil
      (head
        (source-proof-calculus-constructor
          ?owner ProofSequenceNilV1 proof-zero-v1))
      (body
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-calculus-sequence-cons
      (head
        (source-proof-calculus-constructor
          ?owner ProofSequenceConsV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-calculus-environment-nil
      (head
        (source-proof-calculus-constructor
          ?owner ProofEnvironmentNilV1 proof-zero-v1))
      (body
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-calculus-environment-cons
      (head
        (source-proof-calculus-constructor
          ?owner ProofEnvironmentConsV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-calculus-token-literal-class
      (head
        (source-proof-calculus-constructor
          ?owner ProofTokenLiteralClassV1 proof-zero-v1))
      (body
        (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-calculus-token-variable-class
      (head
        (source-proof-calculus-constructor
          ?owner ProofTokenVariableClassV1 proof-zero-v1))
      (body
        (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-calculus-token-equal
      (head
        (source-proof-calculus-constructor
          ?owner ProofTokenEqualV1 proof-zero-v1))
      (body
        (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-calculus-token-unequal
      (head
        (source-proof-calculus-constructor
          ?owner ProofTokenUnequalV1 proof-zero-v1))
      (body
        (source-proof-sequence-functional-layer ?owner ?base)))

    (rule proof-sequence-judgment-append
      (head
        (source-proof-judgment
          ?owner ProofSequenceAppendV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-lookup
      (head
        (source-proof-judgment
          ?owner ProofEnvironmentLookupV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-instantiate
      (head
        (source-proof-judgment
          ?owner ProofSequenceInstantiateV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-lookup-compared
      (head
        (source-proof-judgment
          ?owner ProofEnvironmentLookupComparedV1
          (proof-succ-v1
            (proof-succ-v1
              (proof-succ-v1
                (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-judgment-instantiate-class
      (head
        (source-proof-judgment
          ?owner ProofSequenceInstantiateClassV1
          (proof-succ-v1
            (proof-succ-v1
              (proof-succ-v1
                (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-class
      (head
        (source-proof-judgment
          ?owner ProofTokenClassV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-compare
      (head
        (source-proof-judgment
          ?owner ProofTokenCompareV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-literal
      (head
        (source-proof-judgment
          ?owner ProofTokenLiteralV1 (proof-succ-v1 proof-zero-v1)))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-variable
      (head
        (source-proof-judgment
          ?owner ProofTokenVariableV1 (proof-succ-v1 proof-zero-v1)))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-different
      (head
        (source-proof-judgment
          ?owner ProofTokenDifferentV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-apart
      (head
        (source-proof-judgment
          ?owner ProofTokenApartV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-pair-allowed
      (head
        (source-proof-judgment
          ?owner ProofTokenPairAllowedV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-token-against-sequence
      (head
        (source-proof-judgment
          ?owner ProofTokenAgainstSequenceV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-judgment-support-apart
      (head
        (source-proof-judgment
          ?owner ProofSequenceSupportApartV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body (source-proof-sequence-layer ?owner ?base)))

    (rule proof-sequence-rule-append-nil
      (head
        (source-proof-rule
          ?owner proof-sequence-append-nil-v1
          (proof-list-cons-v1
            (proof-formal-v1 Right proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 ProofSequenceAppendV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 Right)
                (proof-list-cons-v1
                  (proof-fvar-v1 Right) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-append-cons
      (head
        (source-proof-rule
          ?owner proof-sequence-append-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Head proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Right proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 OutTail proof-zero-v1)
                  proof-list-nil-v1))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofSequenceAppendV1
              (proof-list-cons-v1
                (proof-fvar-v1 Tail)
                (proof-list-cons-v1
                  (proof-fvar-v1 Right)
                  (proof-list-cons-v1
                    (proof-fvar-v1 OutTail) proof-list-nil-v1))))
            proof-list-nil-v1)
          (proof-apply-v1 ProofSequenceAppendV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Head)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-fvar-v1 Right)
                (proof-list-cons-v1
                  (proof-apply-v1 ProofSequenceConsV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Head)
                      (proof-list-cons-v1
                        (proof-fvar-v1 OutTail) proof-list-nil-v1)))
                  proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))

    (rule proof-sequence-rule-lookup-head
      (head
        (source-proof-rule
          ?owner proof-environment-lookup-head-v1
          (proof-list-cons-v1
            (proof-formal-v1 Key proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Value proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Tail proof-zero-v1)
                proof-list-nil-v1)))
          proof-list-nil-v1
          (proof-apply-v1 ProofEnvironmentLookupV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofEnvironmentConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Key)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Value)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Tail) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-fvar-v1 Key)
                (proof-list-cons-v1
                  (proof-fvar-v1 Value) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))
    (rule proof-sequence-rule-lookup-tail
      (head
        (source-proof-rule
          ?owner proof-environment-lookup-tail-v1
          (proof-list-cons-v1
            (proof-formal-v1 HeadKey proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 HeadValue proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Tail proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Key proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 Value proof-zero-v1)
                    proof-list-nil-v1)))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenDifferentV1
              (proof-list-cons-v1
                (proof-fvar-v1 HeadKey)
                (proof-list-cons-v1
                  (proof-fvar-v1 Key) proof-list-nil-v1)))
            (proof-list-cons-v1
              (proof-apply-v1 ProofEnvironmentLookupV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Key)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Value) proof-list-nil-v1))))
              proof-list-nil-v1))
          (proof-apply-v1 ProofEnvironmentLookupV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofEnvironmentConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 HeadKey)
                  (proof-list-cons-v1
                    (proof-fvar-v1 HeadValue)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Tail) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-fvar-v1 Key)
                (proof-list-cons-v1
                  (proof-fvar-v1 Value) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))

    (rule proof-sequence-rule-lookup-cons-functional
      (head
        (source-proof-rule
          ?owner proof-environment-lookup-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 HeadKey proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 HeadValue proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Tail proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Key proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 Comparison proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-formal-v1 Value proof-zero-v1)
                      proof-list-nil-v1))))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenCompareV1
              (proof-list-cons-v1
                (proof-fvar-v1 HeadKey)
                (proof-list-cons-v1
                  (proof-fvar-v1 Key)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Comparison) proof-list-nil-v1))))
            (proof-list-cons-v1
              (proof-apply-v1 ProofEnvironmentLookupComparedV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Comparison)
                  (proof-list-cons-v1
                    (proof-fvar-v1 HeadValue)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Tail)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Key)
                        (proof-list-cons-v1
                          (proof-fvar-v1 Value) proof-list-nil-v1))))))
              proof-list-nil-v1))
          (proof-apply-v1 ProofEnvironmentLookupV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofEnvironmentConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 HeadKey)
                  (proof-list-cons-v1
                    (proof-fvar-v1 HeadValue)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Tail) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-fvar-v1 Key)
                (proof-list-cons-v1
                  (proof-fvar-v1 Value) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-rule-lookup-compared-equal-functional
      (head
        (source-proof-rule
          ?owner proof-environment-lookup-compared-equal-v1
          (proof-list-cons-v1
            (proof-formal-v1 HeadValue proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Key proof-zero-v1)
                proof-list-nil-v1)))
          proof-list-nil-v1
          (proof-apply-v1 ProofEnvironmentLookupComparedV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofTokenEqualV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 HeadValue)
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Key)
                    (proof-list-cons-v1
                      (proof-fvar-v1 HeadValue) proof-list-nil-v1))))))
          proof-list-nil-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-rule-lookup-compared-unequal-functional
      (head
        (source-proof-rule
          ?owner proof-environment-lookup-compared-unequal-v1
          (proof-list-cons-v1
            (proof-formal-v1 HeadValue proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Key proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Value proof-zero-v1)
                  proof-list-nil-v1))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofEnvironmentLookupV1
              (proof-list-cons-v1
                (proof-fvar-v1 Tail)
                (proof-list-cons-v1
                  (proof-fvar-v1 Key)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Value) proof-list-nil-v1))))
            proof-list-nil-v1)
          (proof-apply-v1 ProofEnvironmentLookupComparedV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofTokenUnequalV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 HeadValue)
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Key)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Value) proof-list-nil-v1))))))
          proof-list-nil-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))

    (rule proof-sequence-rule-instantiate-nil
      (head
        (source-proof-rule
          ?owner proof-sequence-instantiate-nil-v1
          (proof-list-cons-v1
            (proof-formal-v1 Environment proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 ProofSequenceInstantiateV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 Environment)
                (proof-list-cons-v1
                  (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1)
                  proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-instantiate-literal
      (head
        (source-proof-rule
          ?owner proof-sequence-instantiate-literal-v1
          (proof-list-cons-v1
            (proof-formal-v1 Head proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Environment proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 OutTail proof-zero-v1)
                  proof-list-nil-v1))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenLiteralV1
              (proof-list-cons-v1
                (proof-fvar-v1 Head) proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceInstantiateV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Environment)
                    (proof-list-cons-v1
                      (proof-fvar-v1 OutTail) proof-list-nil-v1))))
              proof-list-nil-v1))
          (proof-apply-v1 ProofSequenceInstantiateV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Head)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-fvar-v1 Environment)
                (proof-list-cons-v1
                  (proof-apply-v1 ProofSequenceConsV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Head)
                      (proof-list-cons-v1
                        (proof-fvar-v1 OutTail) proof-list-nil-v1)))
                  proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))
    (rule proof-sequence-rule-instantiate-variable
      (head
        (source-proof-rule
          ?owner proof-sequence-instantiate-variable-v1
          (proof-list-cons-v1
            (proof-formal-v1 Hole proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Environment proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Image proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 OutTail proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-formal-v1 Out proof-zero-v1)
                      proof-list-nil-v1))))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenVariableV1
              (proof-list-cons-v1
                (proof-fvar-v1 Hole) proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ProofEnvironmentLookupV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Environment)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Hole)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Image) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-apply-v1 ProofSequenceInstantiateV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Environment)
                      (proof-list-cons-v1
                        (proof-fvar-v1 OutTail) proof-list-nil-v1))))
                (proof-list-cons-v1
                  (proof-apply-v1 ProofSequenceAppendV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Image)
                      (proof-list-cons-v1
                        (proof-fvar-v1 OutTail)
                        (proof-list-cons-v1
                          (proof-fvar-v1 Out) proof-list-nil-v1))))
                  proof-list-nil-v1))))
          (proof-apply-v1 ProofSequenceInstantiateV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Hole)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-fvar-v1 Environment)
                (proof-list-cons-v1
                  (proof-fvar-v1 Out) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))

    (rule proof-sequence-rule-instantiate-cons-functional
      (head
        (source-proof-rule
          ?owner proof-sequence-instantiate-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Head proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Environment proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Class proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 Out proof-zero-v1)
                    proof-list-nil-v1)))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenClassV1
              (proof-list-cons-v1
                (proof-fvar-v1 Head)
                (proof-list-cons-v1
                  (proof-fvar-v1 Class) proof-list-nil-v1)))
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceInstantiateClassV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Class)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Head)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Tail)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Environment)
                        (proof-list-cons-v1
                          (proof-fvar-v1 Out) proof-list-nil-v1))))))
              proof-list-nil-v1))
          (proof-apply-v1 ProofSequenceInstantiateV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Head)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-fvar-v1 Environment)
                (proof-list-cons-v1
                  (proof-fvar-v1 Out) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-rule-instantiate-class-literal-functional
      (head
        (source-proof-rule
          ?owner proof-sequence-instantiate-class-literal-v1
          (proof-list-cons-v1
            (proof-formal-v1 Head proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Environment proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 OutTail proof-zero-v1)
                  proof-list-nil-v1))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofSequenceInstantiateV1
              (proof-list-cons-v1
                (proof-fvar-v1 Tail)
                (proof-list-cons-v1
                  (proof-fvar-v1 Environment)
                  (proof-list-cons-v1
                    (proof-fvar-v1 OutTail) proof-list-nil-v1))))
            proof-list-nil-v1)
          (proof-apply-v1 ProofSequenceInstantiateClassV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofTokenLiteralClassV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 Head)
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Environment)
                    (proof-list-cons-v1
                      (proof-apply-v1 ProofSequenceConsV1
                        (proof-list-cons-v1
                          (proof-fvar-v1 Head)
                          (proof-list-cons-v1
                            (proof-fvar-v1 OutTail) proof-list-nil-v1)))
                      proof-list-nil-v1))))))
          proof-list-nil-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-rule-instantiate-class-variable-functional
      (head
        (source-proof-rule
          ?owner proof-sequence-instantiate-class-variable-v1
          (proof-list-cons-v1
            (proof-formal-v1 Hole proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Environment proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Image proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 OutTail proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-formal-v1 Out proof-zero-v1)
                      proof-list-nil-v1))))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofEnvironmentLookupV1
              (proof-list-cons-v1
                (proof-fvar-v1 Environment)
                (proof-list-cons-v1
                  (proof-fvar-v1 Hole)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Image) proof-list-nil-v1))))
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceInstantiateV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Environment)
                    (proof-list-cons-v1
                      (proof-fvar-v1 OutTail) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-apply-v1 ProofSequenceAppendV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Image)
                    (proof-list-cons-v1
                      (proof-fvar-v1 OutTail)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Out) proof-list-nil-v1))))
                proof-list-nil-v1)))
          (proof-apply-v1 ProofSequenceInstantiateClassV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofTokenVariableClassV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 Hole)
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Environment)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Out) proof-list-nil-v1))))))
          proof-list-nil-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))

    (rule proof-sequence-rule-pair-left-literal
      (head
        (source-proof-rule
          ?owner proof-token-pair-left-literal-v1
          (proof-list-cons-v1
            (proof-formal-v1 Left proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Right proof-zero-v1)
              proof-list-nil-v1))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenLiteralV1
              (proof-list-cons-v1
                (proof-fvar-v1 Left) proof-list-nil-v1))
            proof-list-nil-v1)
          (proof-apply-v1 ProofTokenPairAllowedV1
            (proof-list-cons-v1
              (proof-fvar-v1 Left)
              (proof-list-cons-v1
                (proof-fvar-v1 Right) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-pair-right-literal
      (head
        (source-proof-rule
          ?owner proof-token-pair-right-literal-v1
          (proof-list-cons-v1
            (proof-formal-v1 Left proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Right proof-zero-v1)
              proof-list-nil-v1))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenLiteralV1
              (proof-list-cons-v1
                (proof-fvar-v1 Right) proof-list-nil-v1))
            proof-list-nil-v1)
          (proof-apply-v1 ProofTokenPairAllowedV1
            (proof-list-cons-v1
              (proof-fvar-v1 Left)
              (proof-list-cons-v1
                (proof-fvar-v1 Right) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-pair-apart
      (head
        (source-proof-rule
          ?owner proof-token-pair-apart-v1
          (proof-list-cons-v1
            (proof-formal-v1 Left proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Right proof-zero-v1)
              proof-list-nil-v1))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenVariableV1
              (proof-list-cons-v1
                (proof-fvar-v1 Left) proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ProofTokenVariableV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Right) proof-list-nil-v1))
              (proof-list-cons-v1
                (proof-apply-v1 ProofTokenApartV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Left)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Right) proof-list-nil-v1)))
                proof-list-nil-v1)))
          (proof-apply-v1 ProofTokenPairAllowedV1
            (proof-list-cons-v1
              (proof-fvar-v1 Left)
              (proof-list-cons-v1
                (proof-fvar-v1 Right) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))

    (rule proof-sequence-rule-token-against-nil
      (head
        (source-proof-rule
          ?owner proof-token-against-sequence-nil-v1
          (proof-list-cons-v1
            (proof-formal-v1 Token proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenAgainstSequenceV1
            (proof-list-cons-v1
              (proof-fvar-v1 Token)
              (proof-list-cons-v1
                (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1)
                proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-token-against-cons
      (head
        (source-proof-rule
          ?owner proof-token-against-sequence-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Token proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Head proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Tail proof-zero-v1)
                proof-list-nil-v1)))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenPairAllowedV1
              (proof-list-cons-v1
                (proof-fvar-v1 Token)
                (proof-list-cons-v1
                  (proof-fvar-v1 Head) proof-list-nil-v1)))
            (proof-list-cons-v1
              (proof-apply-v1 ProofTokenAgainstSequenceV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Token)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail) proof-list-nil-v1)))
              proof-list-nil-v1))
          (proof-apply-v1 ProofTokenAgainstSequenceV1
            (proof-list-cons-v1
              (proof-fvar-v1 Token)
              (proof-list-cons-v1
                (proof-apply-v1 ProofSequenceConsV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Head)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Tail) proof-list-nil-v1)))
                proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-support-apart-nil
      (head
        (source-proof-rule
          ?owner proof-sequence-support-apart-nil-v1
          (proof-list-cons-v1
            (proof-formal-v1 Right proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 ProofSequenceSupportApartV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 Right) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-rule-support-apart-cons
      (head
        (source-proof-rule
          ?owner proof-sequence-support-apart-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Head proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Tail proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Right proof-zero-v1)
                proof-list-nil-v1)))
          (proof-list-cons-v1
            (proof-apply-v1 ProofTokenAgainstSequenceV1
              (proof-list-cons-v1
                (proof-fvar-v1 Head)
                (proof-list-cons-v1
                  (proof-fvar-v1 Right) proof-list-nil-v1)))
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceSupportApartV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Tail)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Right) proof-list-nil-v1)))
              proof-list-nil-v1))
          (proof-apply-v1 ProofSequenceSupportApartV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Head)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Tail) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-fvar-v1 Right) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))

    ; This role vocabulary is the generated producer ABI.  It gives an
    ; untrusted native evidence producer stable semantic roles without making
    ; constructor, judgment, or rule spellings part of handwritten C.
    (rule proof-sequence-role-constructor-sequence-nil
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base sequence-nil-v1 ProofSequenceNilV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-sequence-cons
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base sequence-cons-v1 ProofSequenceConsV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-environment-nil
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base environment-nil-v1 ProofEnvironmentNilV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-environment-cons
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base environment-cons-v1 ProofEnvironmentConsV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-token-literal-class
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base token-literal-class-v1 ProofTokenLiteralClassV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-token-variable-class
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base token-variable-class-v1 ProofTokenVariableClassV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-token-equal
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base token-equal-v1 ProofTokenEqualV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-constructor-token-unequal
      (head
        (source-proof-sequence-constructor-role
          ?owner ?base token-unequal-v1 ProofTokenUnequalV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))

    (rule proof-sequence-role-judgment-append
      (head
        (source-proof-sequence-judgment-role
          ?owner append-v1 ProofSequenceAppendV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-lookup
      (head
        (source-proof-sequence-judgment-role
          ?owner lookup-v1 ProofEnvironmentLookupV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-instantiate
      (head
        (source-proof-sequence-judgment-role
          ?owner instantiate-v1 ProofSequenceInstantiateV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-lookup-compared
      (head
        (source-proof-sequence-judgment-role
          ?owner lookup-compared-v1 ProofEnvironmentLookupComparedV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-instantiate-class
      (head
        (source-proof-sequence-judgment-role
          ?owner instantiate-class-v1 ProofSequenceInstantiateClassV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-token-class
      (head
        (source-proof-sequence-judgment-role
          ?owner token-class-v1 ProofTokenClassV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-token-compare
      (head
        (source-proof-sequence-judgment-role
          ?owner token-compare-v1 ProofTokenCompareV1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-literal
      (head
        (source-proof-sequence-judgment-role
          ?owner literal-v1 ProofTokenLiteralV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-variable
      (head
        (source-proof-sequence-judgment-role
          ?owner variable-v1 ProofTokenVariableV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-different
      (head
        (source-proof-sequence-judgment-role
          ?owner different-v1 ProofTokenDifferentV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-apart
      (head
        (source-proof-sequence-judgment-role
          ?owner apart-v1 ProofTokenApartV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-pair-allowed
      (head
        (source-proof-sequence-judgment-role
          ?owner pair-allowed-v1 ProofTokenPairAllowedV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-token-against-sequence
      (head
        (source-proof-sequence-judgment-role
          ?owner token-against-sequence-v1
          ProofTokenAgainstSequenceV1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-judgment-support-apart
      (head
        (source-proof-sequence-judgment-role
          ?owner support-apart-v1 ProofSequenceSupportApartV1))
      (body (source-proof-sequence-layer ?owner ?base)))

    (rule proof-sequence-role-rule-append-nil
      (head
        (source-proof-sequence-rule-role
          ?owner append-nil-v1 proof-sequence-append-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-append-cons
      (head
        (source-proof-sequence-rule-role
          ?owner append-cons-v1 proof-sequence-append-cons-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-lookup-head
      (head
        (source-proof-sequence-rule-role
          ?owner lookup-head-v1 proof-environment-lookup-head-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))
    (rule proof-sequence-role-rule-lookup-tail
      (head
        (source-proof-sequence-rule-role
          ?owner lookup-tail-v1 proof-environment-lookup-tail-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))
    (rule proof-sequence-role-rule-lookup-cons
      (head
        (source-proof-sequence-rule-role
          ?owner lookup-cons-v1 proof-environment-lookup-cons-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-rule-lookup-compared-equal
      (head
        (source-proof-sequence-rule-role
          ?owner lookup-compared-equal-v1
          proof-environment-lookup-compared-equal-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-rule-lookup-compared-unequal
      (head
        (source-proof-sequence-rule-role
          ?owner lookup-compared-unequal-v1
          proof-environment-lookup-compared-unequal-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-rule-instantiate-nil
      (head
        (source-proof-sequence-rule-role
          ?owner instantiate-nil-v1 proof-sequence-instantiate-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-instantiate-literal
      (head
        (source-proof-sequence-rule-role
          ?owner instantiate-literal-v1
          proof-sequence-instantiate-literal-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))
    (rule proof-sequence-role-rule-instantiate-variable
      (head
        (source-proof-sequence-rule-role
          ?owner instantiate-variable-v1
          proof-sequence-instantiate-variable-v1))
      (body (source-proof-sequence-relational-layer ?owner ?base)))
    (rule proof-sequence-role-rule-instantiate-cons
      (head
        (source-proof-sequence-rule-role
          ?owner instantiate-cons-v1 proof-sequence-instantiate-cons-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-rule-instantiate-class-literal
      (head
        (source-proof-sequence-rule-role
          ?owner instantiate-class-literal-v1
          proof-sequence-instantiate-class-literal-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-rule-instantiate-class-variable
      (head
        (source-proof-sequence-rule-role
          ?owner instantiate-class-variable-v1
          proof-sequence-instantiate-class-variable-v1))
      (body (source-proof-sequence-functional-layer ?owner ?base)))
    (rule proof-sequence-role-rule-pair-left-literal
      (head
        (source-proof-sequence-rule-role
          ?owner pair-left-literal-v1
          proof-token-pair-left-literal-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-pair-right-literal
      (head
        (source-proof-sequence-rule-role
          ?owner pair-right-literal-v1
          proof-token-pair-right-literal-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-pair-apart
      (head
        (source-proof-sequence-rule-role
          ?owner pair-apart-v1 proof-token-pair-apart-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-token-against-nil
      (head
        (source-proof-sequence-rule-role
          ?owner token-against-nil-v1
          proof-token-against-sequence-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-token-against-cons
      (head
        (source-proof-sequence-rule-role
          ?owner token-against-cons-v1
          proof-token-against-sequence-cons-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-support-apart-nil
      (head
        (source-proof-sequence-rule-role
          ?owner support-apart-nil-v1
          proof-sequence-support-apart-nil-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
    (rule proof-sequence-role-rule-support-apart-cons
      (head
        (source-proof-sequence-rule-role
          ?owner support-apart-cons-v1
          proof-sequence-support-apart-cons-v1))
      (body (source-proof-sequence-layer ?owner ?base)))
  ))
