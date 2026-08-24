; Vocabulary-neutral symbolic tables and actions for the transition compiler.
; Structural bindings are generated from this exact answer stream.

(gslt-presentation-v1 RelationalStateTransitionCompilerCanaryV1
  (signature
    (operator state-table-v1 4)
    (operator state-action-v1 3)
    (operator state-require-depth-v1 1)
    (operator state-insert-unordered-pairs-v1 3)
    (operator state-insert-each-matching-v1 6)
    (operator state-copy-each-row-matching-v1 6)
    (operator state-check-proof-normal-v1 4)
    (operator state-insert-row-v1 3)
    (operator state-row-cons-v1 2)
    (operator state-literal-v1 1)
    (operator state-role-at-v1 2)
    (operator state-role-list-v1 2)
    (operator state-source-column-v1 1)
  )
  (equations)
  (rewrites
    (rule transition-compiler-first-table-v1
      (head
        (state-table-v1
          TransitionCompilerFirstTableV1 2 1 state-scoped-v1))
      (body))
    (rule transition-compiler-second-table-v1
      (head
        (state-table-v1
          TransitionCompilerSecondTableV1 3 1 state-persistent-v1))
      (body))
    (rule transition-compiler-pair-table-v1
      (head
        (state-table-v1
          TransitionCompilerPairTableV1 2 2 state-persistent-v1))
      (body))

    (rule transition-compiler-require-depth-v1
      (head
        (state-action-v1
          TransitionCompilerOperationV1 0
          (state-require-depth-v1 0)))
      (body))
    (rule transition-compiler-pairs-v1
      (head
        (state-action-v1
          TransitionCompilerOperationV1 1
          (state-insert-unordered-pairs-v1
            TransitionCompilerRoleV1 TransitionCompilerPairTableV1
            state-insert-or-require-equal-v1)))
      (body))
    (rule transition-compiler-filtered-insert-v1
      (head
        (state-action-v1
          TransitionCompilerOperationV1 2
          (state-insert-each-matching-v1
            TransitionCompilerRoleV1
            TransitionCompilerFirstTableV1
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 TransitionCompilerLiteralV1)
                state-row-nil-v1))
            TransitionCompilerSecondTableV1
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 TransitionCompilerOtherRoleV1 0)
              (state-row-cons-v1
                state-input-v1
                (state-row-cons-v1
                  (state-literal-v1 TransitionCompilerLiteralV1)
                  state-row-nil-v1))))))
      (body))
    (rule transition-compiler-filtered-copy-v1
      (head
        (state-action-v1
          TransitionCompilerOperationV1 3
          (state-copy-each-row-matching-v1
            TransitionCompilerFirstTableV1
            TransitionCompilerPairTableV1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 1) state-row-nil-v1))
            TransitionCompilerSecondTableV1
            state-require-key-absent-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                state-source-row-index-v1
                (state-row-cons-v1
                  state-source-match-index-v1 state-row-nil-v1))))))
      (body))
    (rule transition-compiler-proof-v1
      (head
        (state-action-v1
          TransitionCompilerOperationV1 4
          (state-check-proof-normal-v1
            TransitionCompilerProofMachineV1
            TransitionCompilerLabelRoleV1
            TransitionCompilerFormulaRoleV1
            TransitionCompilerStepRoleV1)))
      (body))
    (rule transition-compiler-role-list-v1
      (head
        (state-action-v1
          TransitionCompilerOperationV1 5
          (state-insert-row-v1
            TransitionCompilerSecondTableV1
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-list-v1 TransitionCompilerRoleV1 2)
              (state-row-cons-v1
                (state-literal-v1 TransitionCompilerLiteralV1)
                (state-row-cons-v1
                  (state-role-at-v1 TransitionCompilerOtherRoleV1 1)
                  state-row-nil-v1))))))
      (body))
  ))
