; A vocabulary-neutral lifetime canary.  One operation writes and consumes a
; scratch row; the next operation requires that the scratch row no longer
; exists before publishing a persistent observation.

(gslt-presentation-v1 RelationalStateTransactionCanaryV1
  (signature
    (operator source-state-action 3)
    (operator source-fold-role 1)
    (operator source-state-index 1)
    (operator source-state-lifetime 1)
    (operator source-state-literal 1)
    (operator source-state-operation 1)
    (operator source-state-operation-role 2)
    (operator source-state-table 4)
    (operator source-state-write-policy 1)
    (operator state-copy-each-row-matching-v1 6)
    (operator state-insert-each-v1 4)
    (operator state-insert-row-v1 3)
    (operator state-literal-v1 1)
    (operator state-require-row-key-absent-v1 2)
    (operator state-require-row-v1 2)
    (operator state-role-at-v1 2)
    (operator state-row-cons-v1 2)
    (operator state-source-column-v1 1)
  )
  (equations)
  (rewrites
    (rule transaction-canary-lifetime-persistent
      (head (source-state-lifetime state-persistent-v1))
      (body))
    (rule transaction-canary-lifetime-transactional
      (head (source-state-lifetime state-transactional-v1))
      (body))
    (rule transaction-canary-write-policy
      (head
        (source-state-write-policy
          state-insert-or-require-equal-v1))
      (body))

    (rule transaction-canary-scratch-table
      (head
        (source-state-table
          guest-scratch-v1 1 1 state-transactional-v1))
      (body))
    (rule transaction-canary-output-table
      (head
        (source-state-table
          guest-output-v1 1 1 state-persistent-v1))
      (body))
    (rule transaction-canary-dynamic-table
      (head
        (source-state-table
          guest-dynamic-v1 1 1 state-persistent-v1))
      (body))

    (rule transaction-canary-token-literal
      (head (source-state-literal guest-token-v1))
      (body))
    (rule transaction-canary-prime-operation
      (head (source-state-operation guest-prime-v1))
      (body))
    (rule transaction-canary-value-role
      (head (source-fold-role guest-value-role-v1))
      (body))
    (rule transaction-canary-prime-value-role
      (head
        (source-state-operation-role
          guest-prime-v1 guest-value-role-v1))
      (body))
    (rule transaction-canary-index-zero
      (head (source-state-index 0))
      (body))
    (rule transaction-canary-probe-operation
      (head (source-state-operation guest-probe-v1))
      (body))

    (rule transaction-canary-prime-insert
      (head
        (source-state-action
          guest-prime-v1 0
          (state-insert-row-v1
            guest-scratch-v1
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-literal-v1 guest-token-v1)
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-prime-consume
      (head
        (source-state-action
          guest-prime-v1 1
          (state-require-row-v1
            guest-scratch-v1
            (state-row-cons-v1
              (state-literal-v1 guest-token-v1)
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-prime-copy-by-canonical-id
      (head
        (source-state-action
          guest-prime-v1 2
          (state-copy-each-row-matching-v1
            guest-scratch-v1
            guest-scratch-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              state-row-nil-v1)
            guest-output-v1
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-prime-dynamic-absent
      (head
        (source-state-action
          guest-prime-v1 3
          (state-require-row-key-absent-v1
            guest-dynamic-v1
            (state-row-cons-v1
              (state-role-at-v1 guest-value-role-v1 0)
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-prime-dynamic-insert
      (head
        (source-state-action
          guest-prime-v1 4
          (state-insert-each-v1
            guest-value-role-v1
            guest-dynamic-v1
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              state-input-v1
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-prime-dynamic-present
      (head
        (source-state-action
          guest-prime-v1 5
          (state-require-row-v1
            guest-dynamic-v1
            (state-row-cons-v1
              (state-role-at-v1 guest-value-role-v1 0)
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-probe-empty
      (head
        (source-state-action
          guest-probe-v1 0
          (state-require-row-key-absent-v1
            guest-scratch-v1
            (state-row-cons-v1
              (state-literal-v1 guest-token-v1)
              state-row-nil-v1))))
      (body))
    (rule transaction-canary-probe-observe-copy
      (head
        (source-state-action
          guest-probe-v1 1
          (state-require-row-v1
            guest-output-v1
            (state-row-cons-v1
              (state-literal-v1 guest-token-v1)
              state-row-nil-v1))))
      (body))
  ))
