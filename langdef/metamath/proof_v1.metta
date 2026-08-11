; Authored proof-calculus extension over the source-state relations. The
; generated plan carries every table binding, object kind, role, decoder byte,
; radix, and policy consumed by the generic stack-proof executor.

(gslt-presentation-v1 MetamathSourceProofV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-literal-theorem
      (head (source-state-literal mm-label-theorem))
      (body))
    (rule mm-proof-decoder-number-0
      (head (source-state-proof-decoder-number 0))
      (body))
    (rule mm-proof-decoder-number-1
      (head (source-state-proof-decoder-number 1))
      (body))
    (rule mm-proof-decoder-number-5
      (head (source-state-proof-decoder-number 5))
      (body))
    (rule mm-proof-decoder-number-20
      (head (source-state-proof-decoder-number 20))
      (body))
    (rule mm-proof-decoder-number-63
      (head (source-state-proof-decoder-number 63))
      (body))
    (rule mm-proof-decoder-number-65
      (head (source-state-proof-decoder-number 65))
      (body))
    (rule mm-proof-decoder-number-84
      (head (source-state-proof-decoder-number 84))
      (body))
    (rule mm-proof-decoder-number-85
      (head (source-state-proof-decoder-number 85))
      (body))
    (rule mm-proof-decoder-number-89
      (head (source-state-proof-decoder-number 89))
      (body))
    (rule mm-proof-decoder-number-90
      (head (source-state-proof-decoder-number 90))
      (body))
    (rule mm-proof-policy-push-unknown-claim
      (head
        (source-state-proof-unknown-policy
          state-proof-unknown-push-claim-v1))
      (body))

    (rule mm-proof-machine
      (head
        (source-state-proof-machine
          mm-stack-proof-machine-v1
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              mm-state-label-kind
              mm-state-formula
              mm-state-floating-variable
              mm-state-assertion-mandatory-variable
              mm-state-assertion-ordered-hypothesis
              mm-state-assertion-disjoint-variable
              mm-state-active-hypothesis
              mm-state-disjoint-variable
              mm-state-symbol-kind)
            (state-proof-kinds-v1
              (state-literal-v1 mm-label-floating)
              (state-literal-v1 mm-label-essential)
              (state-literal-v1 mm-label-axiom)
              (state-literal-v1 mm-label-theorem)
              (state-literal-v1 mm-symbol-variable)
              (state-byte-literal-v1 63))
            (state-proof-decoder-v1
              65 84 85 89 90 63 20 0 5 1
              state-proof-unknown-push-claim-v1))))
      (body))

    (rule mm-proof-normal-label-not-symbol
      (head
        (source-state-action
          mm-check-theorem-normal 0
          (state-require-row-key-absent-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-proof-normal-label-fresh
      (head
        (source-state-action
          mm-check-theorem-normal 1
          (state-require-row-key-absent-v1
            mm-state-label-name
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-proof-normal-typecode-constant
      (head
        (source-state-action
          mm-check-theorem-normal 2
          (state-require-row-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 0)
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-constant)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-symbols-usable
      (head
        (source-state-action
          mm-check-theorem-normal 3
          (state-require-each-v1
            mm-symbol
            mm-state-expression-symbol
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-proof-normal-collect-formula-variables
      (head
        (source-state-action
          mm-check-theorem-normal 4
          (state-insert-each-matching-v1
            mm-symbol
            mm-state-symbol-kind
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-variable)
                state-row-nil-v1))
            mm-state-assertion-mandatory-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1 state-input-v1 state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-collect-essential-variables
      (head
        (source-state-action
          mm-check-theorem-normal 5
          (state-copy-each-row-v1
            mm-state-active-essential-variable
            mm-state-assertion-mandatory-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-snapshot-hypotheses
      (head
        (source-state-action
          mm-check-theorem-normal 6
          (state-copy-each-row-v1
            mm-state-active-hypothesis
            mm-state-assertion-active-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-order-hypotheses
      (head
        (source-state-action
          mm-check-theorem-normal 7
          (state-copy-each-row-v1
            mm-state-active-hypothesis
            mm-state-assertion-ordered-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-source-row-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 0)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-normal-select-essential-frame-hypotheses
      (head
        (source-state-action
          mm-check-theorem-normal 8
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-label-kind
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-essential)
                state-row-nil-v1))
            mm-state-assertion-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-select-floating-frame-hypotheses
      (head
        (source-state-action
          mm-check-theorem-normal 9
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-order-frame-hypotheses
      (head
        (source-state-action
          mm-check-theorem-normal 10
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-assertion-frame-hypothesis
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1))
            mm-state-assertion-ordered-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-source-match-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 0)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-normal-select-disjoint-left-mandatory
      (head
        (source-state-action
          mm-check-theorem-normal 11
          (state-copy-each-row-matching-v1
            mm-state-disjoint-variable
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1))
            mm-state-assertion-disjoint-left-candidate
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                (state-row-cons-v1
                  (state-source-column-v1 1)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-normal-select-disjoint-right-mandatory
      (head
        (source-state-action
          mm-check-theorem-normal 12
          (state-copy-each-row-matching-v1
            mm-state-assertion-disjoint-left-candidate
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-disjoint-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 1)
                (state-row-cons-v1
                  (state-source-column-v1 2)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-normal-order-disjoint-mandatory
      (head
        (source-state-action
          mm-check-theorem-normal 13
          (state-copy-each-row-matching-v1
            mm-state-assertion-disjoint-left-candidate
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-ordered-disjoint-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                state-source-match-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 1)
                  (state-row-cons-v1
                    (state-source-column-v1 2)
                    state-row-nil-v1)))))))
      (body))
    (rule mm-proof-normal-check
      (head
        (source-state-action
          mm-check-theorem-normal 14
          (state-check-proof-normal-v1
            mm-stack-proof-machine-v1
            mm-label mm-symbol mm-proof-step)))
      (body))
    (rule mm-proof-normal-record-formula
      (head
        (source-state-action
          mm-check-theorem-normal 15
          (state-insert-row-v1
            mm-state-formula
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-role-list-v1 mm-symbol 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-normal-register-label-name
      (head
        (source-state-action
          mm-check-theorem-normal 16
          (state-insert-row-v1
            mm-state-label-name
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-proof-normal-register-label-kind
      (head
        (source-state-action
          mm-check-theorem-normal 17
          (state-insert-row-v1
            mm-state-label-kind
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-theorem)
                state-row-nil-v1)))))
      (body))

    (rule mm-proof-compressed-label-not-symbol
      (head
        (source-state-action
          mm-check-theorem-compressed 0
          (state-require-row-key-absent-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-proof-compressed-label-fresh
      (head
        (source-state-action
          mm-check-theorem-compressed 1
          (state-require-row-key-absent-v1
            mm-state-label-name
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-proof-compressed-typecode-constant
      (head
        (source-state-action
          mm-check-theorem-compressed 2
          (state-require-row-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 0)
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-constant)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-symbols-usable
      (head
        (source-state-action
          mm-check-theorem-compressed 3
          (state-require-each-v1
            mm-symbol
            mm-state-expression-symbol
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-proof-compressed-collect-formula-variables
      (head
        (source-state-action
          mm-check-theorem-compressed 4
          (state-insert-each-matching-v1
            mm-symbol
            mm-state-symbol-kind
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-variable)
                state-row-nil-v1))
            mm-state-assertion-mandatory-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1 state-input-v1 state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-collect-essential-variables
      (head
        (source-state-action
          mm-check-theorem-compressed 5
          (state-copy-each-row-v1
            mm-state-active-essential-variable
            mm-state-assertion-mandatory-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-snapshot-hypotheses
      (head
        (source-state-action
          mm-check-theorem-compressed 6
          (state-copy-each-row-v1
            mm-state-active-hypothesis
            mm-state-assertion-active-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-order-hypotheses
      (head
        (source-state-action
          mm-check-theorem-compressed 7
          (state-copy-each-row-v1
            mm-state-active-hypothesis
            mm-state-assertion-ordered-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-source-row-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 0)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-compressed-select-essential-frame-hypotheses
      (head
        (source-state-action
          mm-check-theorem-compressed 8
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-label-kind
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-essential)
                state-row-nil-v1))
            mm-state-assertion-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-select-floating-frame-hypotheses
      (head
        (source-state-action
          mm-check-theorem-compressed 9
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-order-frame-hypotheses
      (head
        (source-state-action
          mm-check-theorem-compressed 10
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-assertion-frame-hypothesis
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1))
            mm-state-assertion-ordered-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-source-match-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 0)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-compressed-select-disjoint-left-mandatory
      (head
        (source-state-action
          mm-check-theorem-compressed 11
          (state-copy-each-row-matching-v1
            mm-state-disjoint-variable
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1))
            mm-state-assertion-disjoint-left-candidate
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                (state-row-cons-v1
                  (state-source-column-v1 1)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-compressed-select-disjoint-right-mandatory
      (head
        (source-state-action
          mm-check-theorem-compressed 12
          (state-copy-each-row-matching-v1
            mm-state-assertion-disjoint-left-candidate
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-disjoint-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 1)
                (state-row-cons-v1
                  (state-source-column-v1 2)
                  state-row-nil-v1))))))
      (body))
    (rule mm-proof-compressed-order-disjoint-mandatory
      (head
        (source-state-action
          mm-check-theorem-compressed 13
          (state-copy-each-row-matching-v1
            mm-state-assertion-disjoint-left-candidate
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-ordered-disjoint-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                state-source-match-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 1)
                  (state-row-cons-v1
                    (state-source-column-v1 2)
                    state-row-nil-v1)))))))
      (body))
    (rule mm-proof-compressed-check
      (head
        (source-state-action
          mm-check-theorem-compressed 14
          (state-check-proof-compressed-v1
            mm-stack-proof-machine-v1
            mm-label mm-symbol mm-proof-step mm-compressed-word)))
      (body))
    (rule mm-proof-compressed-record-formula
      (head
        (source-state-action
          mm-check-theorem-compressed 15
          (state-insert-row-v1
            mm-state-formula
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-role-list-v1 mm-symbol 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-proof-compressed-register-label-name
      (head
        (source-state-action
          mm-check-theorem-compressed 16
          (state-insert-row-v1
            mm-state-label-name
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-proof-compressed-register-label-kind
      (head
        (source-state-action
          mm-check-theorem-compressed 17
          (state-insert-row-v1
            mm-state-label-kind
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-theorem)
                state-row-nil-v1)))))
      (body))
  ))
