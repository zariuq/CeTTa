; Occurrence-boundary canaries for executing the generated Metamath
; declare-constants operation.  The good occurrence contains two distinct
; constants.  The negative occurrence repeats one interned value and must be
; refused by the authored insertion policy.  This semantic canary realizes
; its source-bound sequence identities as ordinary lists; generated parser
; occurrence gates separately exercise the balanced-tree realization.

(gslt-presentation-v1 MetamathStateTransitionExecutionCanaryV1
  (signature
    (operator OccurrenceRoleValuesV1 3)
    (operator OccurrencePositionV1 2)
    (operator OccurrenceRoleSymbolsV1 3)
    (operator OccurrenceLiteralValueV1 3)
    (operator InterningInjectiveOnV1 2)
    (operator ValueSequenceRealizationV1 2)
    (operator ValuesConsV1 2)
    (operator SymbolsConsV1 2)
    (operator StateValueSequenceV1 2)
  )
  (equations)
  (rewrites
    (rule metamath-constants-good-role-values-v1
      (head
        (OccurrenceRoleValuesV1
          MetamathConstantsGoodOccurrenceV1 mm-symbol
          (StateValueSequenceV1
            MetamathConstantsCanaryStreamV1 NatZeroV1)))
      (body))
    (rule metamath-constants-good-value-sequence-v1
      (head
        (ValueSequenceRealizationV1
          (StateValueSequenceV1
            MetamathConstantsCanaryStreamV1 NatZeroV1)
          (ValuesConsV1 NatZeroV1
            (ValuesConsV1 (NatSuccV1 NatZeroV1) ValuesNilV1))))
      (body))
    (rule metamath-constants-good-position-v1
      (head
        (OccurrencePositionV1
          MetamathConstantsGoodOccurrenceV1
          MetamathConstantsGoodPositionV1))
      (body))
    (rule metamath-constants-good-role-symbols-v1
      (head
        (OccurrenceRoleSymbolsV1
          MetamathConstantsGoodOccurrenceV1 mm-symbol
          (SymbolsConsV1 MetamathConstantFirstV1
            (SymbolsConsV1 MetamathConstantSecondV1 SymbolsNilV1))))
      (body))
    (rule metamath-constants-good-interning-v1
      (head
        (InterningInjectiveOnV1
          (SymbolsConsV1 MetamathConstantFirstV1
            (SymbolsConsV1 MetamathConstantSecondV1 SymbolsNilV1))
          (StateValueSequenceV1
            MetamathConstantsCanaryStreamV1 NatZeroV1)))
      (body))
    (rule metamath-constants-good-literal-v1
      (head
        (OccurrenceLiteralValueV1
          MetamathConstantsGoodOccurrenceV1 mm-symbol-constant
          (NatSuccV1 (NatSuccV1 NatZeroV1))))
      (body))

    (rule metamath-constants-repeated-role-values-v1
      (head
        (OccurrenceRoleValuesV1
          MetamathConstantsRepeatedOccurrenceV1 mm-symbol
          (StateValueSequenceV1
            MetamathConstantsCanaryStreamV1 (NatSuccV1 NatZeroV1))))
      (body))
    (rule metamath-constants-repeated-value-sequence-v1
      (head
        (ValueSequenceRealizationV1
          (StateValueSequenceV1
            MetamathConstantsCanaryStreamV1 (NatSuccV1 NatZeroV1))
          (ValuesConsV1 NatZeroV1
            (ValuesConsV1 NatZeroV1 ValuesNilV1))))
      (body))
    (rule metamath-constants-repeated-position-v1
      (head
        (OccurrencePositionV1
          MetamathConstantsRepeatedOccurrenceV1
          MetamathConstantsRepeatedPositionV1))
      (body))
    (rule metamath-constants-repeated-role-symbols-v1
      (head
        (OccurrenceRoleSymbolsV1
          MetamathConstantsRepeatedOccurrenceV1 mm-symbol
          (SymbolsConsV1 MetamathConstantFirstV1
            (SymbolsConsV1 MetamathConstantFirstV1 SymbolsNilV1))))
      (body))
    (rule metamath-constants-repeated-interning-v1
      (head
        (InterningInjectiveOnV1
          (SymbolsConsV1 MetamathConstantFirstV1
            (SymbolsConsV1 MetamathConstantFirstV1 SymbolsNilV1))
          (StateValueSequenceV1
            MetamathConstantsCanaryStreamV1 (NatSuccV1 NatZeroV1))))
      (body))
    (rule metamath-constants-repeated-literal-v1
      (head
        (OccurrenceLiteralValueV1
          MetamathConstantsRepeatedOccurrenceV1 mm-symbol-constant
          (NatSuccV1 (NatSuccV1 NatZeroV1))))
      (body))
  ))
