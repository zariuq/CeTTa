; Vocabulary-neutral occurrence evidence for the generic state-transition
; relation.  The good occurrence contains three distinct interned values; the
; duplicate occurrence repeats the first value at the third position.

(gslt-presentation-v1 RelationalStateTransitionCanaryV1
  (signature
    (operator OccurrenceRoleValuesV1 3)
    (operator OccurrencePositionV1 2)
    (operator OccurrenceRoleSymbolsV1 3)
    (operator InterningInjectiveOnV1 2)
    (operator StateTableLifetimeV1 3)
    (operator OccurrenceStreamOfV1 2)
  )
  (equations)
  (rewrites
    (rule transition-canary-good-values-v1
      (head
        (OccurrenceRoleValuesV1
          TransitionGoodOccurrenceV1 TransitionRoleV1
          (ValuesConsV1 NatZeroV1
            (ValuesConsV1 (NatSuccV1 NatZeroV1)
              (ValuesConsV1
                (NatSuccV1 (NatSuccV1 NatZeroV1)) ValuesNilV1)))))
      (body))
    (rule transition-canary-good-position-v1
      (head
        (OccurrencePositionV1
          TransitionGoodOccurrenceV1 TransitionGoodPositionV1))
      (body))
    (rule transition-canary-good-symbols-v1
      (head
        (OccurrenceRoleSymbolsV1
          TransitionGoodOccurrenceV1 TransitionRoleV1
          TransitionGoodSymbolsV1))
      (body))
    (rule transition-canary-good-interning-v1
      (head
        (InterningInjectiveOnV1
          TransitionGoodSymbolsV1
          (ValuesConsV1 NatZeroV1
            (ValuesConsV1 (NatSuccV1 NatZeroV1)
              (ValuesConsV1
                (NatSuccV1 (NatSuccV1 NatZeroV1)) ValuesNilV1)))))
      (body))

    (rule transition-canary-duplicate-values-v1
      (head
        (OccurrenceRoleValuesV1
          TransitionDuplicateOccurrenceV1 TransitionRoleV1
          (ValuesConsV1 NatZeroV1
            (ValuesConsV1 (NatSuccV1 NatZeroV1)
              (ValuesConsV1 NatZeroV1 ValuesNilV1)))))
      (body))
    (rule transition-canary-duplicate-position-v1
      (head
        (OccurrencePositionV1
          TransitionDuplicateOccurrenceV1 TransitionDuplicatePositionV1))
      (body))
    (rule transition-canary-duplicate-symbols-v1
      (head
        (OccurrenceRoleSymbolsV1
          TransitionDuplicateOccurrenceV1 TransitionRoleV1
          TransitionDuplicateSymbolsV1))
      (body))
    (rule transition-canary-duplicate-interning-v1
      (head
        (InterningInjectiveOnV1
          TransitionDuplicateSymbolsV1
          (ValuesConsV1 NatZeroV1
            (ValuesConsV1 (NatSuccV1 NatZeroV1)
              (ValuesConsV1 NatZeroV1 ValuesNilV1)))))
      (body))

    (rule transition-canary-scoped-table-v1
      (head
        (StateTableLifetimeV1
          TransitionPackageV1 TransitionTableV1 StateScopedV1))
      (body))
    (rule transition-canary-persistent-table-v1
      (head
        (StateTableLifetimeV1
          TransitionPackageV1
          TransitionPersistentTableV1
          StatePersistentV1))
      (body))
    (rule transition-canary-good-stream-v1
      (head
        (OccurrenceStreamOfV1
          TransitionGoodOccurrenceV1 TransitionCanaryStreamV1))
      (body))
    (rule transition-canary-duplicate-stream-v1
      (head
        (OccurrenceStreamOfV1
          TransitionDuplicateOccurrenceV1 TransitionCanaryStreamV1))
      (body))
  ))
