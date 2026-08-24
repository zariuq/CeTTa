; Extensional occurrence rows consumed by the generic state-transition layer.
; A concrete parser/fold composition may close any of these relations; absent
; rows fail closed at the target boundary.

(gslt-presentation-v1 RelationalStateOccurrenceInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule state-occurrence-interface-role-values-v1
      (head
        (oslf-external-relation-decl-v1 OccurrenceRoleValuesV1 3))
      (body))
    (rule state-occurrence-interface-position-v1
      (head
        (oslf-external-relation-decl-v1 OccurrencePositionV1 2))
      (body))
    (rule state-occurrence-interface-role-symbols-v1
      (head
        (oslf-external-relation-decl-v1 OccurrenceRoleSymbolsV1 3))
      (body))
    (rule state-occurrence-interface-interning-v1
      (head
        (oslf-external-relation-decl-v1 InterningInjectiveOnV1 2))
      (body))
    (rule state-occurrence-interface-value-sequence-v1
      (head
        (oslf-external-relation-decl-v1 ValueSequenceRealizationV1 2))
      (body))
    (rule state-occurrence-interface-role-list-value-v1
      (head
        (oslf-external-relation-decl-v1 OccurrenceRoleListValueV1 4))
      (body))
    (rule state-occurrence-interface-role-nonempty-v1
      (head
        (oslf-external-relation-decl-v1 OccurrenceRoleNonemptyV1 2))
      (body))
    (rule state-occurrence-interface-literal-value-v1
      (head
        (oslf-external-relation-decl-v1 OccurrenceLiteralValueV1 3))
      (body))
    (rule state-occurrence-interface-stream-of-v1
      (head
        (oslf-external-relation-decl-v1 OccurrenceStreamOfV1 2))
      (body))
    (rule state-occurrence-interface-stream-literal-v1
      (head
        (oslf-external-relation-decl-v1 StreamLiteralValueV1 3))
      (body))
    (rule state-occurrence-interface-stream-list-value-v1
      (head
        (oslf-external-relation-decl-v1 StreamListValueV1 3))
      (body))
    (rule state-occurrence-interface-stream-value-bytes-v1
      (head
        (oslf-external-relation-decl-v1 StreamValueBytesV1 3))
      (body))
    (rule state-occurrence-interface-stream-program-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceProgramV1 2))
      (body))
    (rule state-occurrence-interface-stream-empty-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceEmptyV1 1))
      (body))
    (rule state-occurrence-interface-stream-first-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceFirstV1 2))
      (body))
    (rule state-occurrence-interface-stream-next-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceNextV1 3))
      (body))
    (rule state-occurrence-interface-stream-last-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceLastV1 2))
      (body))
    (rule state-occurrence-interface-stream-shape-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceShapeV1 2))
      (body))
    (rule state-occurrence-interface-stream-tail-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceTailV1 3))
      (body))
    (rule state-occurrence-interface-stream-operation-v1
      (head
        (oslf-external-relation-decl-v1 StateOccurrenceOperationV1 2))
      (body))
  ))
