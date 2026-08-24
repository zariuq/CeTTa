; Extensional declarations consumed by the generic state-program calculus.
; Guest packages may close any subset with authored rules; absent declarations
; fail closed when the corresponding compiler branch is selected.

(gslt-presentation-v1 RelationalStateProgramInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule state-program-interface-action-v1
      (head (oslf-external-relation-decl-v1 source-state-action 3))
      (body))
    (rule state-program-interface-depth-v1
      (head (oslf-external-relation-decl-v1 source-state-depth 1))
      (body))
    (rule state-program-interface-final-action-v1
      (head
        (oslf-external-relation-decl-v1 source-state-final-action 2))
      (body))
    (rule state-program-interface-index-v1
      (head (oslf-external-relation-decl-v1 source-state-index 1))
      (body))
    (rule state-program-interface-lifetime-v1
      (head (oslf-external-relation-decl-v1 source-state-lifetime 1))
      (body))
    (rule state-program-interface-literal-v1
      (head (oslf-external-relation-decl-v1 source-state-literal 1))
      (body))
    (rule state-program-interface-proof-decoder-number-v1
      (head
        (oslf-external-relation-decl-v1
          source-state-proof-decoder-number 1))
      (body))
    (rule state-program-interface-proof-action-v1
      (head
        (oslf-external-relation-decl-v1 source-state-proof-action 3))
      (body))
    (rule state-program-interface-proof-machine-v1
      (head
        (oslf-external-relation-decl-v1 source-state-proof-machine 2))
      (body))
    (rule state-program-interface-proof-unknown-policy-v1
      (head
        (oslf-external-relation-decl-v1
          source-state-proof-unknown-policy 1))
      (body))
    (rule state-program-interface-proof-header-policy-v1
      (head
        (oslf-external-relation-decl-v1
          source-state-proof-header-hypothesis-policy 1))
      (body))
    (rule state-program-interface-table-v1
      (head (oslf-external-relation-decl-v1 source-state-table 4))
      (body))
    (rule state-program-interface-write-policy-v1
      (head
        (oslf-external-relation-decl-v1 source-state-write-policy 1))
      (body))
  ))
