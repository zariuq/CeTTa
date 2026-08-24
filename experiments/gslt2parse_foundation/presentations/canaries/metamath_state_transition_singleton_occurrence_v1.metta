(gslt-presentation-v1 MetamathStateTransitionSingletonOccurrenceV1
  (signature
    (operator OccurrenceRoleValuesV1 3)
    (operator OccurrencePositionV1 2)
    (operator ValueSequenceRealizationV1 2)
    (operator StateValueSequenceV1 2)
    (operator ValuesConsV1 2)
  )
  (equations)
  (rewrites
    (rule metamath-constants-singleton-role-values-v1
      (head
        (OccurrenceRoleValuesV1
          MetamathConstantsSingletonOccurrenceV1 mm-symbol
          (StateValueSequenceV1
            MetamathConstantsSingletonStreamV1 NatZeroV1)))
      (body))
    (rule metamath-constants-singleton-value-sequence-v1
      (head
        (ValueSequenceRealizationV1
          (StateValueSequenceV1
            MetamathConstantsSingletonStreamV1 NatZeroV1)
          (ValuesConsV1 NatZeroV1 ValuesNilV1)))
      (body))
    (rule metamath-constants-singleton-position-v1
      (head
        (OccurrencePositionV1
          MetamathConstantsSingletonOccurrenceV1
          MetamathConstantsSingletonPositionV1))
      (body))))
