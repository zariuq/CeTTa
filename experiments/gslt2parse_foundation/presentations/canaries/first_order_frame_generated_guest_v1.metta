; A name-independence canary for the current first-order frame ABI.  Its guest
; identities are deliberately opaque, while its role inventory intentionally
; retains the admitted ABI shape.  Successful admission therefore proves that
; generated artifacts, rather than guest spellings, select the tables; it does
; not establish role-inventory independence.

(gslt-presentation-v1 FirstOrderFrameGeneratedGuestV1
  (signature
    (operator proof-artifact-v1 1)
    (operator proof-calculus-constructor-v1 3)
    (operator proof-extension-v1 2)
    (operator proof-judgment-v1 3)
    (operator proof-succ-v1 1)
    (operator proof-sequence-evidence-artifact-v1 1)
    (operator proof-sequence-evidence-assertion-judgment-role-v1 3)
    (operator proof-sequence-evidence-constructor-role-v1 4)
    (operator proof-sequence-relational-artifact-v1 1)
    (operator proof-sequence-relational-identity-v1 2)
    (operator proof-sequence-relational-table-v1 5)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator source-operator 3)
    (operator source-state-action 3)
    (operator source-state-lifetime 1)
    (operator source-state-literal 1)
    (operator source-state-operation 1)
    (operator source-state-operation-role 2)
    (operator source-state-proof-decoder-number 1)
    (operator source-state-proof-machine 2)
    (operator source-state-proof-unknown-policy 1)
    (operator source-state-table 4)
    (operator state-byte-literal-v1 1)
    (operator state-check-proof-normal-v1 4)
    (operator state-literal-v1 1)
    (operator state-proof-decoder-v1 11)
    (operator state-proof-kinds-v1 6)
    (operator state-proof-tables-v1 9)
    (operator state-stack-proof-machine-v1 3)
  )
  (equations)
  (rewrites
    (rule generated-guest-lifetime-persistent
      (head (source-state-lifetime state-persistent-v1))
      (body))
    (rule generated-guest-lifetime-scoped
      (head (source-state-lifetime state-scoped-v1))
      (body))

    (rule generated-guest-table-00
      (head (source-state-table guest-t00-v1 2 1 state-persistent-v1))
      (body))
    (rule generated-guest-table-01
      (head (source-state-table guest-t01-v1 2 1 state-persistent-v1))
      (body))
    (rule generated-guest-table-02
      (head (source-state-table guest-t02-v1 2 1 state-persistent-v1))
      (body))
    (rule generated-guest-table-03
      (head (source-state-table guest-t03-v1 2 2 state-persistent-v1))
      (body))
    (rule generated-guest-table-04
      (head (source-state-table guest-t04-v1 3 2 state-persistent-v1))
      (body))
    (rule generated-guest-table-05
      (head (source-state-table guest-t05-v1 3 3 state-persistent-v1))
      (body))
    (rule generated-guest-table-06
      (head (source-state-table guest-t06-v1 4 1 state-scoped-v1))
      (body))
    (rule generated-guest-table-07
      (head (source-state-table guest-t07-v1 2 2 state-scoped-v1))
      (body))
    (rule generated-guest-table-08
      (head (source-state-table guest-t08-v1 2 1 state-persistent-v1))
      (body))

    (rule generated-guest-literal-00
      (head (source-state-literal guest-k00-v1))
      (body))
    (rule generated-guest-literal-01
      (head (source-state-literal guest-k01-v1))
      (body))
    (rule generated-guest-literal-02
      (head (source-state-literal guest-k02-v1))
      (body))
    (rule generated-guest-literal-03
      (head (source-state-literal guest-k03-v1))
      (body))
    (rule generated-guest-literal-04
      (head (source-state-literal guest-k04-v1))
      (body))

    (rule generated-guest-decoder-number-00
      (head (source-state-proof-decoder-number 0))
      (body))
    (rule generated-guest-decoder-number-01
      (head (source-state-proof-decoder-number 1))
      (body))
    (rule generated-guest-decoder-number-05
      (head (source-state-proof-decoder-number 5))
      (body))
    (rule generated-guest-decoder-number-20
      (head (source-state-proof-decoder-number 20))
      (body))
    (rule generated-guest-decoder-number-63
      (head (source-state-proof-decoder-number 63))
      (body))
    (rule generated-guest-decoder-number-65
      (head (source-state-proof-decoder-number 65))
      (body))
    (rule generated-guest-decoder-number-84
      (head (source-state-proof-decoder-number 84))
      (body))
    (rule generated-guest-decoder-number-85
      (head (source-state-proof-decoder-number 85))
      (body))
    (rule generated-guest-decoder-number-89
      (head (source-state-proof-decoder-number 89))
      (body))
    (rule generated-guest-decoder-number-90
      (head (source-state-proof-decoder-number 90))
      (body))
    (rule generated-guest-unknown-policy
      (head
        (source-state-proof-unknown-policy
          state-proof-unknown-push-claim-v1))
      (body))

    (rule generated-guest-proof-machine
      (head
        (source-state-proof-machine
          guest-machine-v1
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              guest-t00-v1 guest-t01-v1 guest-t02-v1
              guest-t03-v1 guest-t04-v1 guest-t05-v1
              guest-t06-v1 guest-t07-v1 guest-t08-v1)
            (state-proof-kinds-v1
              (state-literal-v1 guest-k00-v1)
              (state-literal-v1 guest-k01-v1)
              (state-literal-v1 guest-k02-v1)
              (state-literal-v1 guest-k03-v1)
              (state-literal-v1 guest-k04-v1)
              (state-byte-literal-v1 63))
            (state-proof-decoder-v1
              65 84 85 89 90 63 20 0 5 1
              state-proof-unknown-push-claim-v1))))
      (body))

    (rule generated-guest-operation
      (head (source-state-operation guest-check-v1))
      (body))
    (rule generated-guest-operation-role-label
      (head (source-state-operation-role guest-check-v1 guest-r00-v1))
      (body))
    (rule generated-guest-operation-role-formula
      (head (source-state-operation-role guest-check-v1 guest-r01-v1))
      (body))
    (rule generated-guest-operation-role-proof
      (head (source-state-operation-role guest-check-v1 guest-r02-v1))
      (body))
    (rule generated-guest-proof-call
      (head
        (source-state-action
          guest-check-v1 0
          (state-check-proof-normal-v1
            guest-machine-v1 guest-r00-v1 guest-r01-v1 guest-r02-v1)))
      (body))

    (rule generated-guest-proof-extension
      (head
        (proof-artifact-v1
          (proof-extension-v1 GuestProofV1 GuestBaseV1)))
      (body))
    (rule generated-guest-proof-judgment
      (head
        (proof-artifact-v1
          (proof-judgment-v1
            GuestProofV1 GuestProvableV1
            (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule generated-guest-proof-cons
      (head
        (proof-artifact-v1
          (proof-calculus-constructor-v1
            GuestProofV1 GuestConsV1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule generated-guest-proof-nil
      (head
        (proof-artifact-v1
          (proof-calculus-constructor-v1
            GuestProofV1 GuestNilV1 proof-zero-v1)))
      (body))

    (rule generated-guest-evidence-judgment
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-judgment-role-v1
            GuestProofV1 provable-v1 GuestProvableV1)))
      (body))
    (rule generated-guest-evidence-cons
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            GuestProofV1 GuestBaseV1 sequence-cons-v1 GuestConsV1)))
      (body))
    (rule generated-guest-evidence-nil
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            GuestProofV1 GuestBaseV1 sequence-nil-v1 GuestNilV1)))
      (body))

    (rule generated-guest-relational-identity
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-identity-v1
            GuestProofV1 GuestBaseV1)))
      (body))
    (rule generated-guest-relational-table-00
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 label-kind-v1 guest-t00-v1 2 1)))
      (body))
    (rule generated-guest-relational-table-01
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 formula-v1 guest-t01-v1 2 1)))
      (body))
    (rule generated-guest-relational-table-02
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 floating-variable-v1 guest-t02-v1 2 1)))
      (body))
    (rule generated-guest-relational-table-03
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 mandatory-variable-v1 guest-t03-v1 2 2)))
      (body))
    (rule generated-guest-relational-table-04
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 ordered-hypothesis-v1 guest-t04-v1 3 2)))
      (body))
    (rule generated-guest-relational-table-05
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 assertion-disjoint-v1 guest-t05-v1 3 3)))
      (body))
    (rule generated-guest-relational-table-08
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            GuestProofV1 symbol-kind-v1 guest-t08-v1 2 1)))
      (body))

    (rule generated-guest-operator-provable
      (head
        (source-operator
          GuestProofV1 (q-sym GuestProvableV1) (q-succ q-zero)))
      (body))
    (rule generated-guest-operator-cons
      (head
        (source-operator
          GuestProofV1 (q-sym GuestConsV1)
          (q-succ (q-succ q-zero))))
      (body))
    (rule generated-guest-operator-nil
      (head
        (source-operator GuestProofV1 (q-sym GuestNilV1) q-zero))
      (body))
  ))
