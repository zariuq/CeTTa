(gslt-presentation-v1 DirectPeTTaBindingModesV1
  (signature
    (operator ModeListSourceV1 1)
    (operator ModeListMapV1 2)
    (operator ModeGoodOrderV1 1)
    (operator ModeBadOrderV1 1)
    (operator ModeOpenPairV1 2)
    (operator ModeOpenPairUseV1 1)
    (operator ModePartialSourceV1 2)
    (operator ModePartialSourceUseV1 1)
    (operator ModeCompareBitV1 3)
    (operator ModeUnequalV1 2)
    (operator ModeGuardedUseV1 1)
    (operator ModeGuardCompareV1 3)
    (operator ModeGuardCompareUseV1 1)
    (operator ModeOpaqueCompareV1 3)
    (operator ModeOpaqueCompareUseV1 1)
    (operator ModeApartV1 2)
    (operator ModeOpaqueApartV1 2)
    (operator ModeComposedLookupV1 2)
    (operator ModeComposedLookupUseV1 1)
    (operator ModeOverlappingLookupV1 2)
    (operator ModeOverlappingLookupUseV1 1)
    (operator ModeAbsentCapabilityV1 1)
    (operator ModeAbsentCapabilityUseV1 1)
    (operator ModeAbsentThenRelationalUseV1 1)
    (operator ModeBranchingCapabilityV1 1)
    (operator ModeBranchingCapabilityUseV1 1)
    (operator gslt:ModeBranchingCapabilityV1 1)
    (operator ModeBranchingLeftV1 1)
    (operator ModeBranchingRightV1 1)
    (operator ModeDomainViewV1 2)
    (operator ModeDomainViewUseV1 1)
    (operator ModeDomainComposedResultV1 1)
    (operator ModeOverlappingDomainViewV1 2)
    (operator ModeOverlappingDomainViewUseV1 1)
    (operator ModeOverlapComposedResultV1 1)
    (operator ModeGuardSequenceClassV1 2)
    (operator ModeGuardSequenceV1 2)
    (operator ModeGuardSequenceUseV1 1)
    (operator ModeGroundResultGuardV1 2)
    (operator ModeGroundResultGuardUseV1 1)
    (operator ModeGroundResultZeroV1 1)
    (operator ModeRecursiveCompareV1 3)
    (operator ModeRecursiveCompareHeadsV1 4)
    (operator ModeRecursiveCompareUseV1 1)
    (operator ModeRecursiveListConsV1 2)
    (operator ModeRecursiveFoldV1 2)
    (operator ModeRecursiveFoldViewV1 2)
    (operator ModeRecursiveFoldAfterV1 2)
    (operator ModeRecursiveFoldUseV1 1)
    (operator ModeRecursiveFoldConsViewV1 2)
    (operator oslf-external-relation-decl-v1 2)
    (operator metta-equation 2)
    (operator gslt:ModeApartV1 1)
    (operator if 3)
    (operator == 2)
    (operator quote 1)
    (operator metta-nullary 1)
    (operator ModeChooseV1 2)
    (operator ModeChooseUseV1 1)
    (operator ModeConsV1 2)
    (operator ModeCompoundSourceV1 1)
    (operator ModeRelationalCompoundSourceV1 1)
    (operator ModeCompoundLookupV1 2)
    (operator ModeCompoundUseV1 1)
    (operator ModeRelationalCompoundUseV1 1)
    (operator ModeCompoundKeyV1 1)
    (operator ModeCompoundValueV1 1))
  (equations)
  (rewrites
    (rule mode-list-source-v1
      (head
        (ModeListSourceV1
          (ModeConsV1 ModeAtomV1 ModeNilV1)))
      (body))
    (rule mode-list-map-empty-v1
      (head (ModeListMapV1 ModeNilV1 ModeNilV1))
      (body))
    (rule mode-list-map-cons-v1
      (head
        (ModeListMapV1
          (ModeConsV1 ?head ?tail)
          (ModeConsV1 ?head ?mapped-tail)))
      (body
        (ModeListMapV1 ?tail ?mapped-tail)))
    (rule mode-good-order-v1
      (head (ModeGoodOrderV1 ?mapped))
      (body
        (ModeListSourceV1 ?source)
        (ModeListMapV1 ?source ?mapped)))
    (rule mode-bad-order-v1
      (head (ModeBadOrderV1 ?mapped))
      (body
        (ModeListMapV1 ?source ?mapped)
        (ModeListSourceV1 ?source)))
    (rule mode-open-pair-v1
      (head (ModeOpenPairV1 ?known ?unknown))
      (body))
    (rule mode-open-pair-use-v1
      (head (ModeOpenPairUseV1 ?unknown))
      (body
        (ModeListSourceV1 ?known)
        (ModeOpenPairV1 ?known ?unknown)))
    (rule mode-partial-source-v1
      (head (ModePartialSourceV1 ModePartialKnownV1 ?unknown))
      (body))
    (rule mode-partial-source-use-v1
      (head (ModePartialSourceUseV1 ?known))
      (body
        (ModePartialSourceV1 ?known ?ignored)))
    (rule mode-compare-zero-zero-v1
      (head (ModeCompareBitV1 ModeZeroV1 ModeZeroV1 ModeEqualV1))
      (body))
    (rule mode-compare-zero-one-v1
      (head (ModeCompareBitV1 ModeZeroV1 ModeOneV1 ModeLessV1))
      (body))
    (rule mode-compare-one-zero-v1
      (head (ModeCompareBitV1 ModeOneV1 ModeZeroV1 ModeGreaterV1))
      (body))
    (rule mode-compare-one-one-v1
      (head (ModeCompareBitV1 ModeOneV1 ModeOneV1 ModeEqualV1))
      (body))
    (rule mode-unequal-less-v1
      (head (ModeUnequalV1 ?left ?right))
      (body
        (ModeCompareBitV1 ?left ?right ModeLessV1)))
    (rule mode-unequal-greater-v1
      (head (ModeUnequalV1 ?left ?right))
      (body
        (ModeCompareBitV1 ?left ?right ModeGreaterV1)))
    (rule mode-guarded-use-v1
      (head (ModeGuardedUseV1 ModeGuardedYesV1))
      (body
        (ModeUnequalV1 ModeZeroV1 ModeOneV1)))
    (rule mode-apart-capability-v1
      (head (oslf-external-relation-decl-v1 ModeApartV1 2))
      (body))
    (rule mode-opaque-apart-capability-v1
      (head (oslf-external-relation-decl-v1 ModeOpaqueApartV1 2))
      (body))
    (rule mode-composed-lookup-capability-v1
      (head (oslf-external-relation-decl-v1 ModeComposedLookupV1 2))
      (body))
    (rule mode-overlapping-lookup-capability-v1
      (head (oslf-external-relation-decl-v1 ModeOverlappingLookupV1 2))
      (body))
    (rule mode-absent-capability-v1
      (head (oslf-external-relation-decl-v1 ModeAbsentCapabilityV1 1))
      (body))
    (rule mode-branching-capability-v1
      (head (oslf-external-relation-decl-v1 ModeBranchingCapabilityV1 1))
      (body))
    (rule mode-apart-petta-equation-v1
      (head
        (metta-equation
          (gslt:ModeApartV1 (ModeApartV1 ?left ?right))
          (if (== (quote ?left) (quote ?right))
            (metta-nullary empty)
            (ModeApartV1 ?left ?right))))
      (body))
    (rule mode-guard-compare-equal-v1
      (head
        (ModeGuardCompareV1 ?value ?value ModeEqualV1))
      (body))
    (rule mode-guard-compare-apart-v1
      (head
        (ModeGuardCompareV1 ?left ?right ModeDifferentV1))
      (body
        (ModeApartV1 ?left ?right)))
    (rule mode-guard-compare-use-v1
      (head (ModeGuardCompareUseV1 ?result))
      (body
        (ModeGuardCompareV1 ModeZeroV1 ModeOneV1 ?result)))
    (rule mode-opaque-compare-equal-v1
      (head
        (ModeOpaqueCompareV1 ?value ?value ModeEqualV1))
      (body))
    (rule mode-opaque-compare-apart-v1
      (head
        (ModeOpaqueCompareV1 ?left ?right ModeDifferentV1))
      (body
        (ModeOpaqueApartV1 ?left ?right)))
    (rule mode-opaque-compare-use-v1
      (head (ModeOpaqueCompareUseV1 ?result))
      (body
        (ModeOpaqueCompareV1 ModeZeroV1 ModeOneV1 ?result)))
    (rule mode-composed-lookup-zero-v1
      (head
        (ModeComposedLookupV1 ModeLookupZeroV1 ModeLookupValueZeroV1))
      (body))
    (rule mode-composed-lookup-one-v1
      (head
        (ModeComposedLookupV1 ModeLookupOneV1 ModeLookupValueOneV1))
      (body))
    (rule mode-composed-lookup-use-v1
      (head (ModeComposedLookupUseV1 ?value))
      (body
        (ModeComposedLookupV1 ModeLookupZeroV1 ?value)))
    (rule mode-overlapping-lookup-left-v1
      (head
        (ModeOverlappingLookupV1
          ModeOverlapKeyV1 ModeOverlapLeftV1))
      (body))
    (rule mode-overlapping-lookup-right-v1
      (head
        (ModeOverlappingLookupV1
          ModeOverlapKeyV1 ModeOverlapRightV1))
      (body))
    (rule mode-overlapping-lookup-use-v1
      (head (ModeOverlappingLookupUseV1 ?value))
      (body
        (ModeOverlappingLookupV1 ModeOverlapKeyV1 ?value)))
    (rule mode-absent-capability-use-v1
      (head (ModeAbsentCapabilityUseV1 ?value))
      (body
        (ModeAbsentCapabilityV1 ?value)))
    (rule mode-absent-then-relational-use-v1
      (head (ModeAbsentThenRelationalUseV1 ?value))
      (body
        (ModeAbsentCapabilityV1 ModeAbsentKeyV1)
        (ModeOverlappingLookupV1 ModeOverlapKeyV1 ?value)))
    (rule mode-branching-capability-left-v1
      (head
        (metta-equation
          (gslt:ModeBranchingCapabilityV1
            (ModeBranchingCapabilityV1 ?key))
          (ModeBranchingLeftV1 ?key)))
      (body))
    (rule mode-branching-capability-right-v1
      (head
        (metta-equation
          (gslt:ModeBranchingCapabilityV1
            (ModeBranchingCapabilityV1 ?key))
          (ModeBranchingRightV1 ?key)))
      (body))
    (rule mode-branching-capability-use-v1
      (head (ModeBranchingCapabilityUseV1 ModeBranchingWitnessV1))
      (body
        (ModeBranchingCapabilityV1 ModeBranchingKeyV1)))
    (rule mode-domain-view-direct-v1
      (head
        (ModeDomainViewV1 ModeLookupDirectV1 ModeDomainDirectResultV1))
      (body))
    (rule mode-domain-view-composed-v1
      (head
        (ModeDomainViewV1 ?identity (ModeDomainComposedResultV1 ?value)))
      (body
        (ModeComposedLookupV1 ?identity ?value)))
    (rule mode-domain-view-use-v1
      (head (ModeDomainViewUseV1 ?result))
      (body
        (ModeDomainViewV1 ModeLookupZeroV1 ?result)))
    (rule mode-overlapping-domain-view-direct-v1
      (head
        (ModeOverlappingDomainViewV1
          ModeOverlapKeyV1 ModeOverlapDirectResultV1))
      (body))
    (rule mode-overlapping-domain-view-composed-v1
      (head
        (ModeOverlappingDomainViewV1
          ?identity (ModeOverlapComposedResultV1 ?value)))
      (body
        (ModeOverlappingLookupV1 ?identity ?value)))
    (rule mode-overlapping-domain-view-use-v1
      (head (ModeOverlappingDomainViewUseV1 ?result))
      (body
        (ModeOverlappingDomainViewV1 ModeOverlapKeyV1 ?result)))
    (rule mode-guard-sequence-class-zero-v1
      (head
        (ModeGuardSequenceClassV1
          ModeLookupValueZeroV1 ModeSequenceZeroV1))
      (body))
    (rule mode-guard-sequence-class-one-v1
      (head
        (ModeGuardSequenceClassV1
          ModeLookupValueOneV1 ModeSequenceOneV1))
      (body))
    (rule mode-guard-sequence-zero-v1
      (head (ModeGuardSequenceV1 ?key ModeSequenceAcceptedV1))
      (body
        (ModeComposedLookupV1 ?key ?value)
        (ModeGuardSequenceClassV1 ?value ModeSequenceZeroV1)))
    (rule mode-guard-sequence-one-v1
      (head (ModeGuardSequenceV1 ?key ModeSequenceRefusedV1))
      (body
        (ModeComposedLookupV1 ?key ?value)
        (ModeGuardSequenceClassV1 ?value ModeSequenceOneV1)))
    (rule mode-guard-sequence-use-v1
      (head (ModeGuardSequenceUseV1 ?result))
      (body
        (ModeGuardSequenceV1 ModeLookupZeroV1 ?result)))
    (rule mode-ground-result-guard-zero-v1
      (head
        (ModeGroundResultGuardV1
          ?key (ModeGroundResultZeroV1 ?value)))
      (body
        (ModeComposedLookupV1 ?key ?value)
        (ModeGuardSequenceClassV1 ?value ModeSequenceZeroV1)))
    (rule mode-ground-result-guard-other-v1
      (head (ModeGroundResultGuardV1 ?key ?result))
      (body
        (ModeComposedLookupV1 ?key ?value)
        (ModeGuardSequenceClassV1 ?value ModeSequenceOneV1)))
    (rule mode-ground-result-guard-use-v1
      (head (ModeGroundResultGuardUseV1 ?result))
      (body
        (ModeGroundResultGuardV1
          ModeLookupZeroV1 (ModeGroundResultZeroV1 ModeLookupValueZeroV1))
        (ModeComposedLookupV1 ModeLookupZeroV1 ?result)))
    (rule mode-recursive-compare-nil-nil-v1
      (head
        (ModeRecursiveCompareV1
          ModeRecursiveListNilV1 ModeRecursiveListNilV1 ModeEqualV1))
      (body))
    (rule mode-recursive-compare-nil-cons-v1
      (head
        (ModeRecursiveCompareV1
          ModeRecursiveListNilV1
          (ModeRecursiveListConsV1 ?head ?tail)
          ModeDifferentV1))
      (body))
    (rule mode-recursive-compare-cons-nil-v1
      (head
        (ModeRecursiveCompareV1
          (ModeRecursiveListConsV1 ?head ?tail)
          ModeRecursiveListNilV1
          ModeDifferentV1))
      (body))
    (rule mode-recursive-compare-cons-cons-v1
      (head
        (ModeRecursiveCompareV1
          (ModeRecursiveListConsV1 ?left ?leftTail)
          (ModeRecursiveListConsV1 ?right ?rightTail)
          ?result))
      (body
        (ModeCompareBitV1 ?left ?right ?comparison)
        (ModeRecursiveCompareHeadsV1
          ?comparison ?leftTail ?rightTail ?result)))
    (rule mode-recursive-compare-heads-equal-v1
      (head
        (ModeRecursiveCompareHeadsV1
          ModeEqualV1 ?leftTail ?rightTail ?result))
      (body
        (ModeRecursiveCompareV1 ?leftTail ?rightTail ?result)))
    (rule mode-recursive-compare-heads-less-v1
      (head
        (ModeRecursiveCompareHeadsV1
          ModeLessV1 ?leftTail ?rightTail ModeDifferentV1))
      (body))
    (rule mode-recursive-compare-heads-greater-v1
      (head
        (ModeRecursiveCompareHeadsV1
          ModeGreaterV1 ?leftTail ?rightTail ModeDifferentV1))
      (body))
    (rule mode-recursive-compare-use-v1
      (head (ModeRecursiveCompareUseV1 ?result))
      (body
        (ModeRecursiveCompareV1
          (ModeRecursiveListConsV1 ModeZeroV1 ModeRecursiveListNilV1)
          (ModeRecursiveListConsV1 ModeZeroV1 ModeRecursiveListNilV1)
          ?result)))
    (rule mode-recursive-fold-view-nil-v1
      (head
        (ModeRecursiveFoldViewV1
          ModeRecursiveListNilV1 ModeRecursiveFoldEmptyViewV1))
      (body))
    (rule mode-recursive-fold-view-cons-v1
      (head
        (ModeRecursiveFoldViewV1
          (ModeRecursiveListConsV1 ?head ?tail)
          (ModeRecursiveFoldConsViewV1 ?head ?tail)))
      (body))
    (rule mode-recursive-fold-empty-v1
      (head
        (ModeRecursiveFoldV1 ?list ModeRecursiveFoldDoneV1))
      (body
        (ModeRecursiveFoldViewV1
          ?list ModeRecursiveFoldEmptyViewV1)))
    (rule mode-recursive-fold-cons-v1
      (head (ModeRecursiveFoldV1 ?list ?result))
      (body
        (ModeRecursiveFoldViewV1
          ?list (ModeRecursiveFoldConsViewV1 ?head ?tail))
        (ModeRecursiveFoldAfterV1 ?tail ?result)))
    (rule mode-recursive-fold-after-v1
      (head (ModeRecursiveFoldAfterV1 ?tail ?result))
      (body
        (ModeRecursiveFoldV1 ?tail ?result)))
    (rule mode-recursive-fold-use-v1
      (head (ModeRecursiveFoldUseV1 ?result))
      (body
        (ModeRecursiveFoldV1
          (ModeRecursiveListConsV1 ModeZeroV1 ModeRecursiveListNilV1)
          ?result)))
    (rule mode-choose-left-v1
      (head (ModeChooseV1 ModeChoiceKeyV1 ModeChoiceLeftV1))
      (body))
    (rule mode-choose-right-v1
      (head (ModeChooseV1 ModeChoiceKeyV1 ModeChoiceRightV1))
      (body))
    (rule mode-choose-use-v1
      (head (ModeChooseUseV1 ?choice))
      (body
        (ModeChooseV1 ModeChoiceKeyV1 ?choice)))
    (rule mode-compound-source-v1
      (head
        (ModeCompoundSourceV1
          (ModeCompoundKeyV1 ModeAtomV1)))
      (body))
    (rule mode-relational-compound-source-known-v1
      (head
        (ModeRelationalCompoundSourceV1
          (ModeCompoundKeyV1 ModeAtomV1)))
      (body))
    (rule mode-relational-compound-source-other-v1
      (head
        (ModeRelationalCompoundSourceV1
          (ModeCompoundKeyV1 ModeOtherAtomV1)))
      (body))
    (rule mode-compound-lookup-v1
      (head
        (ModeCompoundLookupV1
          (ModeCompoundKeyV1 ModeAtomV1)
          (ModeCompoundValueV1 ModeAnswerV1)))
      (body))
    (rule mode-compound-use-v1
      (head (ModeCompoundUseV1 ?value))
      (body
        (ModeCompoundSourceV1 ?key)
        (ModeCompoundLookupV1 ?key ?value)))
    (rule mode-relational-compound-use-v1
      (head (ModeRelationalCompoundUseV1 ?value))
      (body
        (ModeRelationalCompoundSourceV1 ?key)
        (ModeCompoundLookupV1 ?key ?value)))))
