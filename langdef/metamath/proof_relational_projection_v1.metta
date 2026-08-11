; Assign the generated source-state relations to the neutral chronological
; proof projection.  The projection GSLT, rather than native code, determines
; which rows become active context, stored frame, and apartness evidence.

(gslt-presentation-v1 MetamathProofRelationalProjectionV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-relational-projection-layer-v1
      (head
        (source-proof-relational-projection-layer-v1
          MetamathProofV1 MetamathLanguageV1))
      (body))

    (rule mm-proof-relational-projection-symbol-class-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-symbol-class-v1
          mm-state-symbol-kind))
      (body))
    (rule mm-proof-relational-projection-formula-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-formula-v1
          mm-state-formula))
      (body))
    (rule mm-proof-relational-projection-floating-binding-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-floating-binding-v1
          mm-state-floating-variable))
      (body))
    (rule mm-proof-relational-projection-ordered-context-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-ordered-context-v1
          mm-state-assertion-ordered-hypothesis))
      (body))
    (rule mm-proof-relational-projection-ordered-frame-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-ordered-frame-v1
          mm-state-assertion-ordered-frame-hypothesis))
      (body))
    (rule mm-proof-relational-projection-mandatory-variable-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-mandatory-variable-v1
          mm-state-assertion-mandatory-variable))
      (body))
    (rule mm-proof-relational-projection-assertion-apartness-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-assertion-apartness-v1
          mm-state-assertion-ordered-disjoint-variable))
      (body))
    (rule mm-proof-relational-projection-active-apartness-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-active-apartness-v1
          mm-state-disjoint-variable))
      (body))
    (rule mm-proof-relational-projection-label-class-v1
      (head
        (source-proof-relational-projection-table-role-v1
          MetamathProofV1 proof-relational-label-class-v1
          mm-state-label-kind))
      (body))

    (rule mm-proof-relational-projection-token-literal-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          MetamathProofV1 proof-relational-token-literal-v1
          mm-symbol-constant))
      (body))
    (rule mm-proof-relational-projection-token-variable-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          MetamathProofV1 proof-relational-token-variable-v1
          mm-symbol-variable))
      (body))
    (rule mm-proof-relational-projection-hypothesis-binding-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          MetamathProofV1 proof-relational-hypothesis-binding-v1
          mm-label-floating))
      (body))
    (rule mm-proof-relational-projection-hypothesis-essential-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          MetamathProofV1 proof-relational-hypothesis-essential-v1
          mm-label-essential))
      (body))
    (rule mm-proof-relational-projection-label-axiom-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          MetamathProofV1 proof-relational-label-assertion-v1
          mm-label-axiom))
      (body))
    (rule mm-proof-relational-projection-label-theorem-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          MetamathProofV1 proof-relational-label-assertion-v1
          mm-label-theorem))
      (body))
  ))
