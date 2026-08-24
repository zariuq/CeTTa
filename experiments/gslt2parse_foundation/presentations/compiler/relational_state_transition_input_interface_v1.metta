; Extensional input ABI for lowering a validated symbolic state program.
; Structural-binding generation closes these relations before execution;
; an omitted input relation remains an explicitly empty capability.

(gslt-presentation-v1 RelationalStateTransitionInputInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule state-transition-input-action-v1
      (head (oslf-external-relation-decl-v1 state-action-v1 3))
      (body))
    (rule state-transition-input-final-action-v1
      (head
        (oslf-external-relation-decl-v1 state-final-action-v1 2))
      (body))
    (rule state-transition-input-table-v1
      (head (oslf-external-relation-decl-v1 state-table-v1 4))
      (body))
    (rule state-transition-input-package-v1
      (head
        (oslf-external-relation-decl-v1 StateTransitionPackageV1 1))
      (body))
    (rule state-transition-input-identity-v1
      (head
        (oslf-external-relation-decl-v1 StateDenseIdentityV1 4))
      (body))
    (rule state-transition-input-nat-v1
      (head (oslf-external-relation-decl-v1 StateNatBindingV1 3))
      (body))
    (rule state-transition-input-action-first-v1
      (head (oslf-external-relation-decl-v1 StateActionFirstV1 3))
      (body))
    (rule state-transition-input-action-next-v1
      (head (oslf-external-relation-decl-v1 StateActionNextV1 4))
      (body))
    (rule state-transition-input-action-last-v1
      (head (oslf-external-relation-decl-v1 StateActionLastV1 3))
      (body))
    (rule state-transition-input-final-first-v1
      (head
        (oslf-external-relation-decl-v1 StateFinalActionFirstV1 2))
      (body))
    (rule state-transition-input-final-next-v1
      (head
        (oslf-external-relation-decl-v1 StateFinalActionNextV1 3))
      (body))
    (rule state-transition-input-final-last-v1
      (head
        (oslf-external-relation-decl-v1 StateFinalActionLastV1 2))
      (body))
    (rule state-transition-input-table-first-v1
      (head (oslf-external-relation-decl-v1 StateTableFirstV1 2))
      (body))
    (rule state-transition-input-table-next-v1
      (head (oslf-external-relation-decl-v1 StateTableNextV1 3))
      (body))
    (rule state-transition-input-table-last-v1
      (head (oslf-external-relation-decl-v1 StateTableLastV1 2))
      (body))
  ))
