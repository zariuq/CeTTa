; Representation-only bridge from an immutable parser occurrence stream to
; the extensional relations consumed by the generic state transition GSLT.
; Source data remains constructor data: none of these rules gives an input
; occurrence independent executable meaning.

(gslt-presentation-v1 RelationalStateRuntimeOccurrenceV1
  (signature
    (operator StateRuntimeOccurrenceStreamV1 3)
    (operator StateRuntimeOccurrenceV1 5)
    (operator StateRuntimeRolesConsV1 4)
    (operator StateRuntimeSourceValueV1 2)
    (operator StateOccurrenceIdentityV1 2)
    (operator StateOccurrenceNextTailV1 1)
    (operator StateOccurrenceNonemptyShapeV1 1)
    (operator StateListValueV1 1)
    (operator StateLiteralValueV1 1)
    (operator RuntimeOccurrenceRoleV1 4)
    (operator RuntimeRoleLookupV1 4)
    (operator StateOccurrenceProgramV1 2)
    (operator StateOccurrenceEmptyV1 1)
    (operator StateOccurrenceFirstV1 2)
    (operator StateOccurrenceNextV1 3)
    (operator StateOccurrenceLastV1 2)
    (operator StateOccurrenceShapeV1 2)
    (operator StateOccurrenceTailV1 3)
    (operator StateOccurrenceOperationV1 2)
    (operator OccurrenceRoleValuesV1 3)
    (operator OccurrencePositionV1 2)
    (operator OccurrenceRoleSymbolsV1 3)
    (operator InterningInjectiveOnV1 2)
    (operator OccurrenceRoleListValueV1 4)
    (operator OccurrenceStreamOfV1 2)
    (operator StreamLiteralValueV1 3)
    (operator StreamListValueV1 3)
    (operator StreamValueBytesV1 3)
  )
  (equations)
  (rewrites
    (rule runtime-occurrence-role-head-v1
      (head
        (RuntimeRoleLookupV1
          ?role
          (StateRuntimeRolesConsV1 ?role ?values ?symbols ?rest)
          ?values ?symbols))
      (body))

    (rule runtime-occurrence-role-tail-v1
      (head
        (RuntimeRoleLookupV1
          ?role
          (StateRuntimeRolesConsV1 ?other ?values ?symbols ?rest)
          ?outValues ?outSymbols))
      (body
        (different ?role ?other)
        (RuntimeRoleLookupV1
          ?role ?rest ?outValues ?outSymbols)))

    (rule runtime-occurrence-role-v1
      (head
        (RuntimeOccurrenceRoleV1
          (StateRuntimeOccurrenceV1
            ?identity ?operation ?position ?roles ?tail)
          ?role ?values ?symbols))
      (body
        (RuntimeRoleLookupV1 ?role ?roles ?values ?symbols)))

    (rule runtime-occurrence-program-v1
      (head
        (StateOccurrenceProgramV1
          (StateRuntimeOccurrenceStreamV1 ?package ?identity ?first)
          ?package))
      (body))

    (rule runtime-occurrence-empty-v1
      (head
        (StateOccurrenceEmptyV1
          (StateRuntimeOccurrenceStreamV1
            ?package ?identity StateRuntimeOccurrenceEmptyV1)))
      (body))

    (rule runtime-occurrence-first-v1
      (head
        (StateOccurrenceFirstV1
          (StateRuntimeOccurrenceStreamV1 ?package ?identity
            (StateRuntimeOccurrenceV1
              ?occurrenceIdentity ?operation ?position ?roles ?tail))
          (StateRuntimeOccurrenceV1
            ?occurrenceIdentity ?operation ?position ?roles ?tail)))
      (body))

    (rule runtime-occurrence-next-v1
      (head
        (StateOccurrenceNextV1
          ?stream
          (StateRuntimeOccurrenceV1
            ?identity ?operation ?position ?roles
            (StateOccurrenceNextTailV1 ?next))
          ?next))
      (body))

    (rule runtime-occurrence-last-v1
      (head
        (StateOccurrenceLastV1
          ?stream
          (StateRuntimeOccurrenceV1
            ?identity ?operation ?position ?roles
            StateOccurrenceLastTailV1)))
      (body))

    (rule runtime-occurrence-shape-empty-v1
      (head
        (StateOccurrenceShapeV1
          (StateRuntimeOccurrenceStreamV1
            ?package ?identity StateRuntimeOccurrenceEmptyV1)
          StateOccurrenceEmptyShapeV1))
      (body))

    (rule runtime-occurrence-shape-nonempty-v1
      (head
        (StateOccurrenceShapeV1
          (StateRuntimeOccurrenceStreamV1 ?package ?identity
            (StateRuntimeOccurrenceV1
              ?occurrenceIdentity ?operation ?position ?roles ?tail))
          (StateOccurrenceNonemptyShapeV1
            (StateRuntimeOccurrenceV1
              ?occurrenceIdentity ?operation ?position ?roles ?tail))))
      (body))

    (rule runtime-occurrence-tail-v1
      (head
        (StateOccurrenceTailV1 ?stream
          (StateRuntimeOccurrenceV1
            ?identity ?operation ?position ?roles ?tail)
          ?tail))
      (body))

    (rule runtime-occurrence-operation-v1
      (head
        (StateOccurrenceOperationV1
          (StateRuntimeOccurrenceV1
            ?identity ?operation ?position ?roles ?tail)
          ?operation))
      (body))

    (rule runtime-occurrence-position-v1
      (head
        (OccurrencePositionV1
          (StateRuntimeOccurrenceV1
            ?identity ?operation ?position ?roles ?tail)
          ?position))
      (body))

    (rule runtime-occurrence-role-values-v1
      (head
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values))
      (body
        (RuntimeOccurrenceRoleV1
          ?occurrence ?role ?values ?symbols)))

    (rule runtime-occurrence-role-symbols-v1
      (head
        (OccurrenceRoleSymbolsV1 ?occurrence ?role ?symbols))
      (body
        (RuntimeOccurrenceRoleV1
          ?occurrence ?role ?values ?symbols)))

    ; Runtime source values retain both the canonical interned atom and its
    ; source bytes beneath the generic StateSourceValueV1 constructor.  The
    ; role builder publishes that one lossless carrier as both the semantic
    ; value sequence and the source-symbol sequence.  Thus faithfulness is
    ; witnessed by identity; no reverse search for an occurrence is part of
    ; the representation boundary.
    (rule runtime-source-value-identity-v1
      (head (InterningInjectiveOnV1 ?values ?values))
      (body))

    (rule runtime-occurrence-role-list-v1
      (head
        (OccurrenceRoleListValueV1
          ?occurrence ?role NatZeroV1 (StateListValueV1 ?values)))
      (body
        (RuntimeOccurrenceRoleV1
          ?occurrence ?role ?values ?symbols)))

    (rule runtime-occurrence-stream-of-v1
      (head
        (OccurrenceStreamOfV1
          (StateRuntimeOccurrenceV1
            (StateOccurrenceIdentityV1 ?streamIdentity ?index)
            ?operation ?position ?roles ?tail)
          ?streamIdentity))
      (body))

    (rule runtime-occurrence-literal-v1
      (head
        (StreamLiteralValueV1
          ?stream ?literal (StateLiteralValueV1 ?literal)))
      (body))

    (rule runtime-occurrence-list-value-v1
      (head
        (StreamListValueV1
          ?stream (StateListValueV1 ?values) ?values))
      (body))

    (rule runtime-occurrence-source-bytes-v1
      (head
        (StreamValueBytesV1
          ?stream
          (StateSourceValueV1
            (StateRuntimeSourceValueV1 ?symbol ?bytes))
          ?bytes))
      (body))
  ))
