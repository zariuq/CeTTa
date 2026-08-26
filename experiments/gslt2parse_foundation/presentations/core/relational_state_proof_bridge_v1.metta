; Direct composition from an authored relational state snapshot to the
; chronological proof-trace semantics.  The proof-machine configuration names
; every table and selector.  State, source stream, occurrence, and assertion
; identity remain in the owner term, so facts from distinct snapshots cannot
; be combined by an ambient proof query.

(gslt-presentation-v1 RelationalStateProofBridgeV1
  (signature
    (operator StateProofOwnerV1 6)
    (operator StateProofMachineV1 15)
    (operator StateProofTableEntriesV1 3)
    (operator StateProofSequenceV1 3)
    (operator StateProofLabelsV1 3)
    (operator StateProofCodesV1 3)
    (operator StateProofByteCodesAppendV1 3)
    (operator StateProofFormulaValuesV1 3)
    (operator StateProofFormulaV1 3)
    (operator StateProofContextLabelV1 3)
    (operator StateProofLabelInstructionV1 3)
    (operator StateProofLabelInstructionAxiomComparedV1 6)
    (operator StateProofLabelInstructionTheoremComparedV1 4)
    (operator StateProofLabelCategoryV1 3)
    (operator StateProofLabelCategoryAxiomComparedV1 4)
    (operator StateProofLabelCategoryTheoremComparedV1 2)
    (operator StateProofFirstUnknownLabelV1 2)
    (operator StateProofFirstUnknownLabelComparedV1 5)
    (operator StateProofFirstUnknownLabelLookupV1 5)
    (operator StateProofFirstInactiveHypothesisV1 2)
    (operator StateProofFirstInactiveHypothesisCategoryV1 5)
    (operator StateProofFirstInactiveHypothesisMembershipV1 5)
    (operator StateProofContextLabelEntriesV1 5)
    (operator StateProofContextLabelEntriesAssertionComparedV1 7)
    (operator StateProofContextLabelEntriesLabelComparedV1 6)
    (operator StateProofContextLabelMembershipV1 4)
    (operator StateProofContextLabelMembershipAssertionComparedV1 6)
    (operator StateProofContextLabelMembershipLabelComparedV1 5)
    (operator StateProofBuildContextV1 5)
    (operator StateProofBuildContextComparedV1 7)
    (operator StateProofBuildFrameV1 5)
    (operator StateProofBuildFrameAssertionComparedV1 7)
    (operator StateProofBuildFrameKindComparedV1 7)
    (operator StateProofBuildFrameFloatingV1 6)
    (operator StateProofBuildFrameFloatingMembershipV1 8)
    (operator StateProofFrameEntryV1 4)
    (operator StateProofBuildApartnessV1 4)
    (operator StateProofBuildApartnessComparedV1 7)
    (operator StateProofAssertionV1 4)
    (operator StateProofUsableAssertionKindV1 3)
    (operator StateProofUsableAssertionKindComparedV1 3)
    (operator StateProofUsableAssertionTheoremComparedV1 1)
    (operator StateProofTokenClassComparedV1 2)
    (operator StateProofTokenComparisonV1 2)
    (operator StateProofTokenLiteralComparedV1 1)
    (operator StateProofUnequalComparedV1 1)
    (operator StateProofCurrentApartDecisionV1 4)
    (operator StateProofCurrentApartEntriesDecisionV1 4)
    (operator StateProofUnknownByteV1 2)
    (operator StateProofNormalAcceptedV1 4)
    (operator StateProofNormalResultV1 5)
    (operator StateProofCompressedAcceptedV1 6)
    (operator StateProofCompressedResultV1 7)
    (operator StateProofCheckNormalV1 7)
    (operator StateProofCheckNormalResultV1 8)
    (operator StateProofCheckCompressedV1 8)
    (operator StateProofCheckCompressedResultV1 9)
    (operator StateProofStepResultV1 6)
    (operator CheckedProofV1 1)
    (operator CheckedProofRefusedV1 2)
    (operator HistoryContainsCheckedProofV1 2)
    (operator RefusedProofCheckV1 1)
    (operator OutcomeClassV1 2)

    (operator ByteValuesConsV1 2)
    (operator StreamListValueV1 3)
    (operator StreamValueBytesV1 3)
    (operator ProofRelationalLabelV1 2)
    (operator ProofRelationalTokenV1 2)
    (operator ProofRelationalAssertionV1 3)
    (operator ProofTraceLabelConsV1 2)
    (operator ProofTraceCodeConsV1 2)
  )
  (equations)
  (rewrites
    ; The symbolic machine was admitted by the state-package compiler.  Its
    ; tables and literal selectors are lowered through the same source-bound
    ; dense identities used by the state action program.
    (rule state-proof-machine-v1
      (head
        (StateProofMachineV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label-kind ?formula ?floating-variable ?mandatory-variable
          ?assertion-hypothesis ?assertion-disjoint ?active-hypothesis
          ?active-disjoint ?symbol-kind ?floating-kind ?essential-kind
          ?axiom-kind ?theorem-kind ?variable-kind))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?raw-label-kind ?raw-formula ?raw-floating-variable
              ?raw-mandatory-variable ?raw-assertion-hypothesis
              ?raw-assertion-disjoint ?raw-active-hypothesis
              ?raw-active-disjoint ?raw-symbol-kind)
            (state-proof-kinds-v1
              (state-literal-v1 ?raw-floating-kind)
              (state-literal-v1 ?raw-essential-kind)
              (state-literal-v1 ?raw-axiom-kind)
              (state-literal-v1 ?raw-theorem-kind)
              (state-literal-v1 ?raw-variable-kind)
              (state-byte-literal-v1 ?unknown-byte))
            ?decoder))
        (StateDenseIdentityV1
          ?package StateTableKindV1 ?raw-label-kind ?label-kind)
        (StateDenseIdentityV1
          ?package StateTableKindV1 ?raw-formula ?formula)
        (StateDenseIdentityV1
          ?package StateTableKindV1
          ?raw-floating-variable ?floating-variable)
        (StateDenseIdentityV1
          ?package StateTableKindV1
          ?raw-mandatory-variable ?mandatory-variable)
        (StateDenseIdentityV1
          ?package StateTableKindV1
          ?raw-assertion-hypothesis ?assertion-hypothesis)
        (StateDenseIdentityV1
          ?package StateTableKindV1
          ?raw-assertion-disjoint ?assertion-disjoint)
        (StateDenseIdentityV1
          ?package StateTableKindV1
          ?raw-active-hypothesis ?active-hypothesis)
        (StateDenseIdentityV1
          ?package StateTableKindV1
          ?raw-active-disjoint ?active-disjoint)
        (StateDenseIdentityV1
          ?package StateTableKindV1 ?raw-symbol-kind ?symbol-kind)
        (StreamLiteralValueV1
          ?stream ?raw-floating-kind ?floating-kind)
        (StreamLiteralValueV1
          ?stream ?raw-essential-kind ?essential-kind)
        (StreamLiteralValueV1
          ?stream ?raw-axiom-kind ?axiom-kind)
        (StreamLiteralValueV1
          ?stream ?raw-theorem-kind ?theorem-kind)
        (StreamLiteralValueV1
          ?stream ?raw-variable-kind ?variable-kind)))

    (rule state-proof-table-entries-v1
      (head
        (StateProofTableEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?table ?entries))
      (body
        (StateTablesOfV1 ?state ?tables)
        (TableOfV1 ?tables ?table (FoundTableV1 ?entries))))

    ; Values remain interned state values.  Only their proof-facing wrappers
    ; change, and the owner prevents cross-snapshot unification.
    (rule state-proof-sequence-nil-v1
      (head
        (StateProofSequenceV1
          ?owner ?values ProofSequenceNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule state-proof-sequence-cons-v1
      (head
        (StateProofSequenceV1
          ?owner ?values
          (ProofSequenceConsV1
            (ProofRelationalTokenV1 ?owner ?value) ?sequence)))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (StateProofSequenceV1 ?owner ?rest ?sequence)))

    (rule state-proof-labels-nil-v1
      (head
        (StateProofLabelsV1
          ?owner ?values ProofTraceLabelNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule state-proof-labels-cons-v1
      (head
        (StateProofLabelsV1
          ?owner ?values
          (ProofTraceLabelConsV1
            (ProofRelationalLabelV1 ?owner ?value) ?labels)))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (StateProofLabelsV1 ?owner ?rest ?labels)))

    ; A parser value denotes its exact source bytes.  Concatenating the byte
    ; lists of the code role handles either one compressed word or several
    ; lexical chunks without assigning that choice to the proof backend.
    (rule state-proof-byte-codes-append-nil-v1
      (head
        (StateProofByteCodesAppendV1
          ByteValuesNilV1 ?tail ?tail))
      (body))
    (rule state-proof-byte-codes-append-cons-v1
      (head
        (StateProofByteCodesAppendV1
          (ByteValuesConsV1 ?byte ?bytes) ?tail
          (ProofTraceCodeConsV1 ?byte ?codes)))
      (body
        (StateProofByteCodesAppendV1 ?bytes ?tail ?codes)))
    (rule state-proof-codes-nil-v1
      (head
        (StateProofCodesV1
          ?owner ?values ProofTraceCodeNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule state-proof-codes-cons-v1
      (head
        (StateProofCodesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?values ?codes))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (StreamValueBytesV1 ?stream ?value ?bytes)
        (StateProofCodesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?rest ?tail)
        (StateProofByteCodesAppendV1 ?bytes ?tail ?codes)))

    (rule state-proof-formula-values-v1
      (head
        (StateProofFormulaValuesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label ?values))
      (body
        (StateProofMachineV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?formula ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?list-value RowNilV1))))
        (StreamListValueV1 ?stream ?list-value ?values)))
    (rule state-proof-formula-v1
      (head
        (StateProofFormulaV1 ?owner ?label ?formula))
      (body
        (StateProofFormulaValuesV1 ?owner ?label ?values)
        (StateProofSequenceV1 ?owner ?values ?formula)))

    ; Chronological context: every active hypothesis copied into the current
    ; assertion's ordered-hypothesis table, preserving insertion order.
    (rule state-proof-build-context-nil-v1
      (head
        (StateProofBuildContextV1
          ?owner ?assertion EntriesNilV1
          ProofTraceContextNilV1 ProofTraceLabelNilV1))
      (body))
    (rule state-proof-build-context-cons-v1
      (head
        (StateProofBuildContextV1
          ?owner ?assertion
          (EntriesConsV1
            (EntryV1 ?sequence
              (RowConsV1 ?other-assertion
                (RowConsV1 ?position
                  (RowConsV1 ?label RowNilV1))) ?depth)
            ?rest)
          ?context ?labels))
      (body
        (StateValueCompareV1 ?other-assertion ?assertion ?comparison)
        (StateProofBuildContextComparedV1
          ?comparison ?owner ?assertion ?label ?rest ?context ?labels)))
    (rule state-proof-build-context-equal-v1
      (head
        (StateProofBuildContextComparedV1
          CompareEqualV1 ?owner ?assertion ?label ?rest
          (ProofTraceContextConsV1 ?formula ?context)
          (ProofTraceLabelConsV1
            (ProofRelationalLabelV1 ?owner ?label) ?labels)))
      (body
        (StateProofFormulaV1 ?owner ?label ?formula)
        (StateProofBuildContextV1
          ?owner ?assertion ?rest ?context ?labels)))
    (rule state-proof-build-context-different-v1
      (head
        (StateProofBuildContextComparedV1
          CompareDifferentV1 ?owner ?assertion ?label ?rest ?context ?labels))
      (body
        (StateProofBuildContextV1
          ?owner ?assertion ?rest ?context ?labels)))

    (rule state-proof-context-label-entries-cons-v1
      (head
        (StateProofContextLabelEntriesV1
          ?owner ?assertion ?label
          (EntriesConsV1
            (EntryV1 ?sequence
              (RowConsV1 ?other-assertion
                (RowConsV1 ?position
                  (RowConsV1 ?row-label RowNilV1))) ?depth)
            ?rest)
          ?formula))
      (body
        (StateValueCompareV1 ?other-assertion ?assertion ?comparison)
        (StateProofContextLabelEntriesAssertionComparedV1
          ?comparison ?owner ?assertion ?label ?row-label ?rest
          ?formula)))
    (rule state-proof-context-label-entries-assertion-equal-v1
      (head
        (StateProofContextLabelEntriesAssertionComparedV1
          CompareEqualV1 ?owner ?assertion ?label ?row-label ?rest
          ?formula))
      (body
        (StateValueCompareV1 ?row-label ?label ?comparison)
        (StateProofContextLabelEntriesLabelComparedV1
          ?comparison ?owner ?assertion ?label ?rest ?formula)))
    (rule state-proof-context-label-entries-assertion-different-v1
      (head
        (StateProofContextLabelEntriesAssertionComparedV1
          CompareDifferentV1 ?owner ?assertion ?label ?row-label ?rest
          ?formula))
      (body
        (StateProofContextLabelEntriesV1
          ?owner ?assertion ?label ?rest ?formula)))
    (rule state-proof-context-label-entries-label-equal-v1
      (head
        (StateProofContextLabelEntriesLabelComparedV1
          CompareEqualV1 ?owner ?assertion ?label ?rest
          ?formula))
      (body
        (StateProofFormulaV1 ?owner ?label ?formula)))
    (rule state-proof-context-label-entries-label-different-v1
      (head
        (StateProofContextLabelEntriesLabelComparedV1
          CompareDifferentV1 ?owner ?assertion ?label ?rest ?formula))
      (body
        (StateProofContextLabelEntriesV1
          ?owner ?assertion ?label ?rest ?formula)))

    ; Total membership over the ordered assertion/hypothesis rows.  The
    ; positive context-label projection above remains the formula-producing
    ; interface; this relation supplies the explicit absent branch needed by
    ; proof-refusal semantics.
    (rule state-proof-context-label-membership-nil-v1
      (head
        (StateProofContextLabelMembershipV1
          ?assertion ?label EntriesNilV1 RowMissingV1))
      (body))
    (rule state-proof-context-label-membership-cons-v1
      (head
        (StateProofContextLabelMembershipV1
          ?assertion ?label
          (EntriesConsV1
            (EntryV1 ?sequence
              (RowConsV1 ?other-assertion
                (RowConsV1 ?position
                  (RowConsV1 ?row-label RowNilV1))) ?depth)
            ?rest)
          ?membership))
      (body
        (StateValueCompareV1
          ?other-assertion ?assertion ?comparison)
        (StateProofContextLabelMembershipAssertionComparedV1
          ?comparison ?assertion ?label ?row-label ?rest ?membership)))
    (rule state-proof-context-label-membership-assertion-equal-v1
      (head
        (StateProofContextLabelMembershipAssertionComparedV1
          CompareEqualV1 ?assertion ?label ?row-label ?rest ?membership))
      (body
        (StateValueCompareV1 ?row-label ?label ?comparison)
        (StateProofContextLabelMembershipLabelComparedV1
          ?comparison ?assertion ?label ?rest ?membership)))
    (rule state-proof-context-label-membership-assertion-different-v1
      (head
        (StateProofContextLabelMembershipAssertionComparedV1
          CompareDifferentV1 ?assertion ?label ?row-label ?rest
          ?membership))
      (body
        (StateProofContextLabelMembershipV1
          ?assertion ?label ?rest ?membership)))
    (rule state-proof-context-label-membership-label-equal-v1
      (head
        (StateProofContextLabelMembershipLabelComparedV1
          CompareEqualV1 ?assertion ?label ?rest RowPresentV1))
      (body))
    (rule state-proof-context-label-membership-label-different-v1
      (head
        (StateProofContextLabelMembershipLabelComparedV1
          CompareDifferentV1 ?assertion ?label ?rest ?membership))
      (body
        (StateProofContextLabelMembershipV1
          ?assertion ?label ?rest ?membership)))
    (rule state-proof-context-label-v1
      (head
        (StateProofContextLabelV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label ?formula))
      (body
        (StateProofMachineV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label-kind ?formula-table ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion-hypothesis ?entries)
        (StateProofContextLabelEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion ?label ?entries ?formula)))

    ; Mandatory assertion frame.  Essential hypotheses are retained; floating
    ; hypotheses are retained exactly when their variable is mandatory.
    (rule state-proof-frame-floating-v1
      (head
        (StateProofFrameEntryV1
          ?owner ?assertion ?label
          (ProofTraceFrameVariableV1
            (ProofRelationalTokenV1 ?owner ?variable)
            (ProofRelationalTokenV1 ?owner ?typecode))))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?kind-entries)
        (EntriesLookupKeyV1
          ?kind-entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?floating-kind RowNilV1))))
        (StateProofTableEntriesV1
          ?owner ?floating-variable ?floating-entries)
        (EntriesLookupKeyV1
          ?floating-entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?variable RowNilV1))))
        (StateProofTableEntriesV1
          ?owner ?mandatory-variable ?mandatory-entries)
        (EntriesRowMemberV1
          ?mandatory-entries
          (RowConsV1 ?assertion (RowConsV1 ?variable RowNilV1))
          RowPresentV1)
        (StateProofFormulaValuesV1
          ?owner ?label ?formula-values)
        (ValuesExactlyTwoV1 ?formula-values ?typecode ?variable)))
    (rule state-proof-frame-essential-v1
      (head
        (StateProofFrameEntryV1
          ?owner ?assertion ?label
          (ProofTraceFrameEssentialV1 ?formula-sequence)))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?kind-entries)
        (EntriesLookupKeyV1
          ?kind-entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?essential-kind RowNilV1))))
        (StateProofFormulaV1 ?owner ?label ?formula-sequence)))

    (rule state-proof-build-frame-nil-v1
      (head
        (StateProofBuildFrameV1
          ?owner ?assertion EntriesNilV1
          ProofTraceFrameNilV1 ProofTraceLabelNilV1))
      (body))
    (rule state-proof-build-frame-cons-v1
      (head
        (StateProofBuildFrameV1
          ?owner ?assertion
          (EntriesConsV1
            (EntryV1 ?sequence
              (RowConsV1 ?other-assertion
                (RowConsV1 ?position
                  (RowConsV1 ?label RowNilV1))) ?depth)
            ?rest)
          ?frame ?labels))
      (body
        (StateValueCompareV1 ?other-assertion ?assertion ?comparison)
        (StateProofBuildFrameAssertionComparedV1
          ?comparison ?owner ?assertion ?label ?rest ?frame ?labels)))
    (rule state-proof-build-frame-assertion-equal-v1
      (head
        (StateProofBuildFrameAssertionComparedV1
          CompareEqualV1 ?owner ?assertion ?label ?rest ?frame ?labels))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?kind-entries)
        (EntriesLookupKeyV1
          ?kind-entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?kind RowNilV1))))
        (StateValueCompareV1 ?kind ?essential-kind ?comparison)
        (StateProofBuildFrameKindComparedV1
          ?comparison ?owner ?assertion ?label ?rest ?frame ?labels)))
    (rule state-proof-build-frame-assertion-different-v1
      (head
        (StateProofBuildFrameAssertionComparedV1
          CompareDifferentV1 ?owner ?assertion ?label ?rest ?frame ?labels))
      (body
        (StateProofBuildFrameV1
          ?owner ?assertion ?rest ?frame ?labels)))
    (rule state-proof-build-frame-kind-essential-v1
      (head
        (StateProofBuildFrameKindComparedV1
          CompareEqualV1 ?owner ?assertion ?label ?rest
          (ProofTraceFrameConsV1
            (ProofTraceFrameEssentialV1 ?formula) ?frame)
          (ProofTraceLabelConsV1
            (ProofRelationalLabelV1 ?owner ?label) ?labels)))
      (body
        (StateProofFormulaV1 ?owner ?label ?formula)
        (StateProofBuildFrameV1
          ?owner ?assertion ?rest ?frame ?labels)))
    (rule state-proof-build-frame-kind-floating-different-v1
      (head
        (StateProofBuildFrameKindComparedV1
          CompareDifferentV1 ?owner ?assertion ?label ?rest ?frame ?labels))
      (body
        (StateProofBuildFrameFloatingV1
          ?owner ?assertion ?label ?rest ?frame ?labels)))
    (rule state-proof-build-frame-floating-v1
      (head
        (StateProofBuildFrameFloatingV1
          ?owner ?assertion ?label ?rest ?frame ?labels))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1
          ?owner ?floating-variable ?floating-entries)
        (EntriesLookupKeyV1
          ?floating-entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?variable RowNilV1))))
        (StateProofTableEntriesV1
          ?owner ?mandatory-variable ?mandatory-entries)
        (EntriesRowMemberV1
          ?mandatory-entries
          (RowConsV1 ?assertion (RowConsV1 ?variable RowNilV1))
          ?membership)
        (StateProofBuildFrameFloatingMembershipV1
          ?membership ?owner ?assertion ?label ?variable ?rest
          ?frame ?labels)))
    (rule state-proof-build-frame-floating-mandatory-v1
      (head
        (StateProofBuildFrameFloatingMembershipV1
          RowPresentV1 ?owner ?assertion ?label ?variable ?rest
          (ProofTraceFrameConsV1
            (ProofTraceFrameVariableV1
              (ProofRelationalTokenV1 ?owner ?variable)
              (ProofRelationalTokenV1 ?owner ?typecode))
            ?frame)
          (ProofTraceLabelConsV1
            (ProofRelationalLabelV1 ?owner ?label) ?labels)))
      (body
        (StateProofFormulaValuesV1
          ?owner ?label ?formula-values)
        (ValuesExactlyTwoV1 ?formula-values ?typecode ?variable)
        (StateProofBuildFrameV1
          ?owner ?assertion ?rest ?frame ?labels)))
    (rule state-proof-build-frame-floating-nonmandatory-v1
      (head
        (StateProofBuildFrameFloatingMembershipV1
          RowMissingV1 ?owner ?assertion ?label ?variable ?rest
          ?frame ?labels))
      (body
        (StateProofBuildFrameV1
          ?owner ?assertion ?rest ?frame ?labels)))

    ; Apartness has set meaning, but the oldest-first table traversal gives a
    ; deterministic evidence order without imposing it on the judgment.
    (rule state-proof-build-apartness-nil-v1
      (head
        (StateProofBuildApartnessV1
          ?owner ?assertion EntriesNilV1 ProofAssertionListNilV1))
      (body))
    (rule state-proof-build-apartness-cons-v1
      (head
        (StateProofBuildApartnessV1
          ?owner ?assertion
          (EntriesConsV1
            (EntryV1 ?sequence
              (RowConsV1 ?other-assertion
                (RowConsV1 ?left
                  (RowConsV1 ?right RowNilV1))) ?depth)
            ?rest)
          ?apartness))
      (body
        (StateValueCompareV1 ?other-assertion ?assertion ?comparison)
        (StateProofBuildApartnessComparedV1
          ?comparison ?owner ?assertion ?left ?right ?rest ?apartness)))
    (rule state-proof-build-apartness-equal-v1
      (head
        (StateProofBuildApartnessComparedV1
          CompareEqualV1 ?owner ?assertion ?left ?right ?rest
          (ProofAssertionListConsV1
            (ProofAssertionDisjointV1
              (ProofRelationalTokenV1 ?owner ?left)
              (ProofRelationalTokenV1 ?owner ?right))
            ?apartness)))
      (body
        (StateProofBuildApartnessV1
          ?owner ?assertion ?rest ?apartness)))
    (rule state-proof-build-apartness-different-v1
      (head
        (StateProofBuildApartnessComparedV1
          CompareDifferentV1 ?owner ?assertion ?left ?right ?rest ?apartness))
      (body
        (StateProofBuildApartnessV1
          ?owner ?assertion ?rest ?apartness)))

    (rule state-proof-usable-assertion-kind-v1
      (head
        (StateProofUsableAssertionKindV1
          ?kind ?axiom-kind ?theorem-kind))
      (body
        (StateValueCompareV1 ?kind ?axiom-kind ?comparison)
        (StateProofUsableAssertionKindComparedV1
          ?comparison ?kind ?theorem-kind)))
    (rule state-proof-usable-assertion-kind-axiom-v1
      (head
        (StateProofUsableAssertionKindComparedV1
          CompareEqualV1 ?kind ?theorem-kind))
      (body))
    (rule state-proof-usable-assertion-kind-nonaxiom-v1
      (head
        (StateProofUsableAssertionKindComparedV1
          CompareDifferentV1 ?kind ?theorem-kind))
      (body
        (StateValueCompareV1 ?kind ?theorem-kind ?comparison)
        (StateProofUsableAssertionTheoremComparedV1 ?comparison)))
    (rule state-proof-usable-assertion-kind-theorem-v1
      (head
        (StateProofUsableAssertionTheoremComparedV1 CompareEqualV1))
      (body))
    (rule state-proof-assertion-v1
      (head
        (StateProofAssertionV1
          ?owner ?label
          (ProofAssertionV1
            ?variables ?essentials
            (ProofRelationalTokenV1 ?owner ?typecode)
            ?body ?apartness)
          ?frame))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?kind-entries)
        (EntriesLookupKeyV1
          ?kind-entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?kind RowNilV1))))
        (StateProofUsableAssertionKindV1
          ?kind ?axiom-kind ?theorem-kind)
        (StateProofFormulaValuesV1
          ?owner ?label ?formula-values)
        (ValuesHeadTailV1 ?formula-values ?typecode ?body-values)
        (StateProofSequenceV1 ?owner ?body-values ?body)
        (StateProofTableEntriesV1
          ?owner ?assertion-hypothesis ?hypothesis-entries)
        (StateProofBuildFrameV1
          ?owner ?label ?hypothesis-entries ?frame ?initial-labels)
        (ProofTraceFrameProjectV1 ?frame ?variables ?essentials)
        (StateProofTableEntriesV1
          ?owner ?assertion-disjoint ?disjoint-entries)
        (StateProofBuildApartnessV1
          ?owner ?label ?disjoint-entries ?apartness)))

    (rule state-proof-unknown-byte-v1
      (head
        (StateProofUnknownByteV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?unknown-byte))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            ?tables
            (state-proof-kinds-v1
              ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
              ?variable-kind (state-byte-literal-v1 ?unknown-byte))
            ?decoder))))

    ; Proof-trace input facts are projections of the owned state snapshot.
    (rule state-proof-token-class-v1
      (head
        (source-proof-trace-input-token-class-v1
          (ProofRelationalTokenV1 ?owner ?token) ?class))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?symbol-kind ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?token RowNilV1)
          (FoundRowV1
            (RowConsV1 ?token (RowConsV1 ?kind RowNilV1))))
        (StateValueCompareV1 ?kind ?variable-kind ?comparison)
        (StateProofTokenClassComparedV1 ?comparison ?class)))
    (rule state-proof-token-class-variable-v1
      (head
        (StateProofTokenClassComparedV1
          CompareEqualV1 ProofTokenVariableClassV1))
      (body))
    (rule state-proof-token-class-literal-v1
      (head
        (StateProofTokenClassComparedV1
          CompareDifferentV1 ProofTokenLiteralClassV1))
      (body))
    (rule state-proof-token-compare-v1
      (head
        (source-proof-trace-input-token-compare-v1
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right)
          ?result))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateValueCompareV1 ?left ?right ?comparison)
        (StateProofTokenComparisonV1 ?comparison ?result)))
    (rule state-proof-token-compare-equal-v1
      (head
        (StateProofTokenComparisonV1
          CompareEqualV1 ProofTokenEqualV1))
      (body))
    (rule state-proof-token-compare-unequal-v1
      (head
        (StateProofTokenComparisonV1
          CompareDifferentV1 ProofTokenUnequalV1))
      (body))
    (rule state-proof-token-variable-v1
      (head
        (source-proof-trace-input-token-variable-v1
          (ProofRelationalTokenV1 ?owner ?token)))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?symbol-kind ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?token RowNilV1)
          (FoundRowV1
            (RowConsV1 ?token (RowConsV1 ?variable-kind RowNilV1))))))
    (rule state-proof-token-literal-v1
      (head
        (source-proof-trace-input-token-literal-v1
          (ProofRelationalTokenV1 ?owner ?token)))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?symbol-kind ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?token RowNilV1)
          (FoundRowV1
            (RowConsV1 ?token (RowConsV1 ?kind RowNilV1))))
        (StateValueCompareV1 ?kind ?variable-kind ?comparison)
        (StateProofTokenLiteralComparedV1 ?comparison)))
    (rule state-proof-token-literal-different-v1
      (head
        (StateProofTokenLiteralComparedV1 CompareDifferentV1))
      (body))
    (rule state-proof-unequal-v1
      (head
        (StateProofUnequalComparedV1 CompareDifferentV1))
      (body))
    (rule state-proof-token-different-v1
      (head
        (source-proof-trace-input-token-different-v1
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right)))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateValueCompareV1 ?left ?right ?comparison)
        (StateProofUnequalComparedV1 ?comparison)))
    (rule state-proof-label-different-v1
      (head
        (source-proof-trace-input-label-different-v1
          (ProofRelationalLabelV1 ?owner ?left)
          (ProofRelationalLabelV1 ?owner ?right)))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateValueCompareV1 ?left ?right ?comparison)
        (StateProofUnequalComparedV1 ?comparison)))
    (rule state-proof-current-apart-v1
      (head
        (source-proof-trace-input-current-apart-v1
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right)))
      (body
        (StateProofCurrentApartDecisionV1
          ?owner ?left ?right ProofTraceApartPresentV1)))
    (rule state-proof-current-apart-decision-v1
      (head
        (source-proof-trace-input-current-apart-decision-v1
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right) ?decision))
      (body
        (StateProofCurrentApartDecisionV1
          ?owner ?left ?right ?decision)))
    (rule state-proof-current-apart-from-state-v1
      (head
        (StateProofCurrentApartDecisionV1
          ?owner ?left ?right ?decision))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?active-disjoint ?entries)
        (StateProofCurrentApartEntriesDecisionV1
          ?entries ?left ?right ?decision)))
    (rule state-proof-current-apart-forward-present-v1
      (head
        (StateProofCurrentApartEntriesDecisionV1
          ?entries ?left ?right ProofTraceApartPresentV1))
      (body
        (EntriesRowMemberV1
          ?entries
          (RowConsV1 ?left (RowConsV1 ?right RowNilV1))
          RowPresentV1)))
    (rule state-proof-current-apart-reverse-present-v1
      (head
        (StateProofCurrentApartEntriesDecisionV1
          ?entries ?left ?right ProofTraceApartPresentV1))
      (body
        (EntriesRowMemberV1
          ?entries
          (RowConsV1 ?left (RowConsV1 ?right RowNilV1))
          RowMissingV1)
        (EntriesRowMemberV1
          ?entries
          (RowConsV1 ?right (RowConsV1 ?left RowNilV1))
          RowPresentV1)))
    (rule state-proof-current-apart-missing-v1
      (head
        (StateProofCurrentApartEntriesDecisionV1
          ?entries ?left ?right ProofTraceApartMissingV1))
      (body
        (EntriesRowMemberV1
          ?entries
          (RowConsV1 ?left (RowConsV1 ?right RowNilV1))
          RowMissingV1)
        (EntriesRowMemberV1
          ?entries
          (RowConsV1 ?right (RowConsV1 ?left RowNilV1))
          RowMissingV1)))
    (rule state-proof-label-instruction-v1
      (head
        (source-proof-trace-label-instruction-v1
          (ProofRelationalLabelV1 ?owner ?label)
          ?instruction))
      (body
        (StateProofLabelInstructionV1
          ?owner ?label ?instruction)))
    (rule state-proof-label-instruction-class-v1
      (head
        (StateProofLabelInstructionV1
          ?owner ?label ?instruction))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?kind RowNilV1))))
        (StateValueCompareV1 ?kind ?axiom-kind ?comparison)
        (StateProofLabelInstructionAxiomComparedV1
          ?comparison ?kind ?theorem-kind ?owner ?label ?instruction)))
    (rule state-proof-label-instruction-axiom-v1
      (head
        (StateProofLabelInstructionAxiomComparedV1
          CompareEqualV1 ?kind ?theorem-kind ?owner ?label
          (ProofTraceApplyV1
            (ProofRelationalAssertionV1 ?owner ?label ?assertion)
            ?frame)))
      (body
        (StateProofAssertionV1 ?owner ?label ?assertion ?frame)))
    (rule state-proof-label-instruction-nonaxiom-v1
      (head
        (StateProofLabelInstructionAxiomComparedV1
          CompareDifferentV1 ?kind ?theorem-kind ?owner ?label ?instruction))
      (body
        (StateValueCompareV1 ?kind ?theorem-kind ?comparison)
        (StateProofLabelInstructionTheoremComparedV1
          ?comparison ?owner ?label ?instruction)))
    (rule state-proof-label-instruction-theorem-v1
      (head
        (StateProofLabelInstructionTheoremComparedV1
          CompareEqualV1 ?owner ?label
          (ProofTraceApplyV1
            (ProofRelationalAssertionV1 ?owner ?label ?assertion)
            ?frame)))
      (body
        (StateProofAssertionV1 ?owner ?label ?assertion ?frame)))
    (rule state-proof-label-instruction-hypothesis-v1
      (head
        (StateProofLabelInstructionTheoremComparedV1
          CompareDifferentV1 ?owner ?label (ProofTracePushV1 ?formula)))
      (body
        (StateProofContextLabelV1 ?owner ?label ?formula)))

    (rule state-proof-label-category-v1
      (head
        (StateProofLabelCategoryV1 ?owner ?label ?category))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1)
          (FoundRowV1
            (RowConsV1 ?label (RowConsV1 ?kind RowNilV1))))
        (StateValueCompareV1 ?kind ?axiom-kind ?comparison)
        (StateProofLabelCategoryAxiomComparedV1
          ?comparison ?kind ?theorem-kind ?category)))
    (rule state-proof-label-category-axiom-v1
      (head
        (StateProofLabelCategoryAxiomComparedV1
          CompareEqualV1 ?kind ?theorem-kind StateProofLabelAxiomV1))
      (body))
    (rule state-proof-label-category-nonaxiom-v1
      (head
        (StateProofLabelCategoryAxiomComparedV1
          CompareDifferentV1 ?kind ?theorem-kind ?category))
      (body
        (StateValueCompareV1 ?kind ?theorem-kind ?comparison)
        (StateProofLabelCategoryTheoremComparedV1
          ?comparison ?category)))
    (rule state-proof-label-category-theorem-v1
      (head
        (StateProofLabelCategoryTheoremComparedV1
          CompareEqualV1 StateProofLabelTheoremV1))
      (body))
    (rule state-proof-label-category-hypothesis-v1
      (head
        (StateProofLabelCategoryTheoremComparedV1
          CompareDifferentV1 StateProofLabelHypothesisV1))
      (body))

    ; The authored incomplete-proof marker is not an unknown label.  Classify
    ; it before consulting the finite label table so the incomplete and
    ; unknown-label paths are disjoint.  For ordinary labels, retain the first
    ; missing source label so a failed compilation has an explicit semantic
    ; result instead of disappearing as an unanswered relation.
    (rule state-proof-first-unknown-label-v1
      (head
        (StateProofFirstUnknownLabelV1
          (ProofTraceLabelConsV1
            (ProofRelationalLabelV1
              (StateProofOwnerV1
                ?machine ?package ?state ?stream ?occurrence ?assertion)
              ?label)
            ?rest)
          ?unknown))
      (body
        (StateProofUnknownByteV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?unknown-byte)
        (StreamValueBytesV1 ?stream ?label ?bytes)
        (StateValueCompareV1
          ?bytes
          (ByteValuesConsV1 ?unknown-byte ByteValuesNilV1)
          ?comparison)
        (StateProofFirstUnknownLabelComparedV1
          ?comparison
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label ?rest ?unknown)))
    (rule state-proof-first-unknown-label-incomplete-v1
      (head
        (StateProofFirstUnknownLabelComparedV1
          CompareEqualV1 ?owner ?label ?rest ?unknown))
      (body
        (StateProofFirstUnknownLabelV1 ?rest ?unknown)))
    (rule state-proof-first-unknown-label-ordinary-v1
      (head
        (StateProofFirstUnknownLabelComparedV1
          CompareDifferentV1 ?owner ?label ?rest ?unknown))
      (body
        (StateProofMachineV1
          ?owner ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1 ?owner ?label-kind ?entries)
        (EntriesLookupKeyV1
          ?entries (NatSuccV1 NatZeroV1)
          (RowConsV1 ?label RowNilV1) ?lookup)
        (StateProofFirstUnknownLabelLookupV1
          ?lookup ?owner ?label ?rest ?unknown)))
    (rule state-proof-first-unknown-label-absent-v1
      (head
        (StateProofFirstUnknownLabelLookupV1
          RowAbsentV1 ?owner ?label ?rest ?label))
      (body))
    (rule state-proof-first-unknown-label-found-v1
      (head
        (StateProofFirstUnknownLabelLookupV1
          (FoundRowV1 ?row) ?owner ?label ?rest ?unknown))
      (body
        (StateProofFirstUnknownLabelV1 ?rest ?unknown)))

    (rule state-proof-first-inactive-hypothesis-v1
      (head
        (StateProofFirstInactiveHypothesisV1
          (ProofTraceLabelConsV1
            (ProofRelationalLabelV1 ?owner ?label) ?rest)
          ?inactive))
      (body
        (StateProofLabelCategoryV1 ?owner ?label ?category)
        (StateProofFirstInactiveHypothesisCategoryV1
          ?category ?owner ?label ?rest ?inactive)))
    (rule state-proof-first-inactive-hypothesis-axiom-v1
      (head
        (StateProofFirstInactiveHypothesisCategoryV1
          StateProofLabelAxiomV1 ?owner ?label ?rest ?inactive))
      (body
        (StateProofFirstInactiveHypothesisV1 ?rest ?inactive)))
    (rule state-proof-first-inactive-hypothesis-theorem-v1
      (head
        (StateProofFirstInactiveHypothesisCategoryV1
          StateProofLabelTheoremV1 ?owner ?label ?rest ?inactive))
      (body
        (StateProofFirstInactiveHypothesisV1 ?rest ?inactive)))
    (rule state-proof-first-inactive-hypothesis-hypothesis-v1
      (head
        (StateProofFirstInactiveHypothesisCategoryV1
          StateProofLabelHypothesisV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label ?rest ?inactive))
      (body
        (StateProofMachineV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion-hypothesis ?entries)
        (StateProofContextLabelMembershipV1
          ?assertion ?label ?entries ?membership)
        (StateProofFirstInactiveHypothesisMembershipV1
          ?membership
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label ?rest ?inactive)))
    (rule state-proof-first-inactive-hypothesis-missing-v1
      (head
        (StateProofFirstInactiveHypothesisMembershipV1
          RowMissingV1 ?owner ?label ?rest ?label))
      (body))
    (rule state-proof-first-inactive-hypothesis-present-v1
      (head
        (StateProofFirstInactiveHypothesisMembershipV1
          RowPresentV1 ?owner ?label ?rest ?inactive))
      (body
        (StateProofFirstInactiveHypothesisV1 ?rest ?inactive)))
    (rule state-proof-apply-owned-assertion-result-v1
      (head
        (ProofTraceApplyAssertionResultV1
          (ProofRelationalAssertionV1 ?owner ?label ?assertion)
          ?frame ?input ?result))
      (body
        (StateProofAssertionV1
          ?owner ?label ?assertion ?frame)
        (ProofTraceRealizeAssertionResultV1
          ?assertion ?frame ?input ?result)))
    (rule state-proof-incomplete-normal-label-v1
      (head
        (source-proof-trace-input-incomplete-normal-label-v1
          (ProofRelationalLabelV1
            (StateProofOwnerV1
              ?machine ?package ?state ?stream ?occurrence ?assertion)
            ?label)))
      (body
        (StateProofUnknownByteV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?unknown-byte)
        (StreamValueBytesV1
          ?stream ?label
          (ByteValuesConsV1 ?unknown-byte ByteValuesNilV1))))

    ; Complete and explicitly incomplete proofs share one result interface.
    ; Acceptance remains a projection; final-stack refusals retain their
    ; result constructor and trace as evidence.
    (rule state-proof-normal-result-v1
      (head
        (StateProofNormalResultV1
          ?context ?labels ?target ?result ?trace))
      (body
        (ProofTraceCompileNormalStatusV1
          ?labels ?target ?status ?trace)
        (ProofTraceCheckResultV1 ?context ?trace ?target ?result)))
    (rule state-proof-normal-unknown-label-result-v1
      (head
        (StateProofNormalResultV1
          ?context ?labels ?target
          (ProofTraceRefusedUnknownLabelV1 ?label)
          ProofTraceNilV1))
      (body
        (StateProofFirstUnknownLabelV1 ?labels ?label)))
    (rule state-proof-normal-inactive-hypothesis-result-v1
      (head
        (StateProofNormalResultV1
          ?context ?labels ?target
          (ProofTraceRefusedInactiveHypothesisV1 ?label)
          ProofTraceNilV1))
      (body
        (StateProofFirstInactiveHypothesisV1 ?labels ?label)))
    (rule state-proof-normal-accepted-v1
      (head
        (StateProofNormalAcceptedV1
          ?context ?labels ?target ?trace))
      (body
        (StateProofNormalResultV1
          ?context ?labels ?target ProofTraceSucceededV1 ?trace)))
    (rule state-proof-compressed-result-v1
      (head
        (StateProofCompressedResultV1
          ?context ?initial ?explicit ?codes ?target ?result ?trace))
      (body
        (ProofTraceCompileDictionaryV1
          ?initial ?explicit ?dictionary)
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ProofTraceNatZeroV1
          ProofTracePhaseBetweenV1 ?target
          ?status ?trace)
        (ProofTraceCheckResultV1 ?context ?trace ?target ?result)))
    (rule state-proof-compressed-unknown-label-result-v1
      (head
        (StateProofCompressedResultV1
          ?context ?initial ?explicit ?codes ?target
          (ProofTraceRefusedUnknownLabelV1 ?label)
          ProofTraceNilV1))
      (body
        (StateProofFirstUnknownLabelV1 ?explicit ?label)))
    (rule state-proof-compressed-inactive-hypothesis-result-v1
      (head
        (StateProofCompressedResultV1
          ?context ?initial ?explicit ?codes ?target
          (ProofTraceRefusedInactiveHypothesisV1 ?label)
          ProofTraceNilV1))
      (body
        (StateProofFirstInactiveHypothesisV1 ?explicit ?label)))
    (rule state-proof-compressed-explicit-mandatory-result-v1
      (head
        (StateProofCompressedResultV1
          ?context ?initial ?explicit ?codes ?target
          (ProofTraceRefusedExplicitMandatoryHypothesisV1 ?label)
          ProofTraceNilV1))
      (body
        (ProofTraceHeaderFirstOverlapV1 ?initial ?explicit ?label)))
    (rule state-proof-compressed-decode-refusal-result-v1
      (head
        (StateProofCompressedResultV1
          ?context ?initial ?explicit ?codes ?target
          ?reason ProofTraceNilV1))
      (body
        (ProofTraceCompileDictionaryV1
          ?initial ?explicit ?dictionary)
        (ProofTraceDecodeFirstRefusalV1
          ?codes ProofTraceNatZeroV1 ProofTracePhaseBetweenV1 ?reason)
        (ProofTraceResultIsRefusalV1 ?reason)))
    (rule state-proof-compressed-accepted-v1
      (head
        (StateProofCompressedAcceptedV1
          ?context ?initial ?explicit ?codes ?target ?trace))
      (body
        (StateProofCompressedResultV1
          ?context ?initial ?explicit ?codes ?target
          ProofTraceSucceededV1 ?trace)))

    (rule state-proof-check-normal-result-v1
      (head
        (StateProofCheckNormalResultV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?proof-role ?result ?trace))
      (body
        (StatePackageOfV1 ?state ?package)
        (OccurrenceStreamOfV1 ?occurrence ?stream)
        (OccurrenceRoleValuesV1
          ?occurrence ?label-role ?label-values)
        (ValuesSingletonV1 ?label-values ?assertion)
        (OccurrenceRoleValuesV1
          ?occurrence ?formula-role ?target-values)
        (OccurrenceRoleValuesV1
          ?occurrence ?proof-role ?proof-values)
        (StateProofMachineV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion-hypothesis ?hypothesis-entries)
        (StateProofBuildContextV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion ?hypothesis-entries ?context ?context-labels)
        (StateProofSequenceV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?target-values ?target)
        (StateProofLabelsV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?proof-values ?labels)
        (StateProofNormalResultV1
          ?context ?labels ?target ?result ?trace)))
    (rule state-proof-check-normal-v1
      (head
        (StateProofCheckNormalV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?proof-role ?trace))
      (body
        (StateProofCheckNormalResultV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?proof-role ProofTraceSucceededV1 ?trace)))
    (rule state-proof-check-compressed-result-v1
      (head
        (StateProofCheckCompressedResultV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?header-role ?code-role ?result ?trace))
      (body
        (StatePackageOfV1 ?state ?package)
        (OccurrenceStreamOfV1 ?occurrence ?stream)
        (OccurrenceRoleValuesV1
          ?occurrence ?label-role ?label-values)
        (ValuesSingletonV1 ?label-values ?assertion)
        (OccurrenceRoleValuesV1
          ?occurrence ?formula-role ?target-values)
        (OccurrenceRoleValuesV1
          ?occurrence ?header-role ?header-values)
        (OccurrenceRoleValuesV1
          ?occurrence ?code-role ?code-values)
        (StateProofMachineV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?label-kind ?formula ?floating-variable
          ?mandatory-variable ?assertion-hypothesis ?assertion-disjoint
          ?active-hypothesis ?active-disjoint ?symbol-kind
          ?floating-kind ?essential-kind ?axiom-kind ?theorem-kind
          ?variable-kind)
        (StateProofTableEntriesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion-hypothesis ?hypothesis-entries)
        (StateProofBuildContextV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion ?hypothesis-entries ?context ?context-labels)
        (StateProofBuildFrameV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?assertion ?hypothesis-entries ?frame ?initial-labels)
        (StateProofSequenceV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?target-values ?target)
        (StateProofLabelsV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?header-values ?explicit-labels)
        (StateProofCodesV1
          (StateProofOwnerV1
            ?machine ?package ?state ?stream ?occurrence ?assertion)
          ?code-values ?codes)
        (StateProofCompressedResultV1
          ?context ?initial-labels ?explicit-labels ?codes ?target
          ?result ?trace)))
    (rule state-proof-check-compressed-v1
      (head
        (StateProofCheckCompressedV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?header-role ?code-role ?trace))
      (body
        (StateProofCheckCompressedResultV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?header-role ?code-role ProofTraceSucceededV1 ?trace)))

    (rule state-step-check-proof-normal-v1
      (head
        (StateStepV1
          ?package ?state ?occurrence
          (StateCheckProofNormalV1
            ?machine ?label-role ?formula-role ?proof-role)
          ?outcome ?state ?history))
      (body
        (StatePackageOfV1 ?state ?package)
        (OccurrencePositionV1 ?occurrence ?position)
        (StateProofCheckNormalResultV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?proof-role ?result ?trace)
        (StateProofStepResultV1
          (StateCheckProofNormalV1
            ?machine ?label-role ?formula-role ?proof-role)
          ?position ?result ?trace ?outcome ?history)))
    (rule state-step-check-proof-compressed-v1
      (head
        (StateStepV1
          ?package ?state ?occurrence
          (StateCheckProofCompressedV1
            ?machine ?label-role ?formula-role ?header-role ?code-role)
          ?outcome ?state ?history))
      (body
        (StatePackageOfV1 ?state ?package)
        (OccurrencePositionV1 ?occurrence ?position)
        (StateProofCheckCompressedResultV1
          ?state ?occurrence ?machine ?label-role ?formula-role
          ?header-role ?code-role ?result ?trace)
        (StateProofStepResultV1
          (StateCheckProofCompressedV1
            ?machine ?label-role ?formula-role ?header-role ?code-role)
          ?position ?result ?trace ?outcome ?history)))

    (rule state-proof-step-result-accepted-v1
      (head
        (StateProofStepResultV1
          ?action ?position ProofTraceSucceededV1 ?trace AcceptedV1
          (HistoryConsV1
            (EnteredActionV1 ?action ?position)
            (HistoryConsV1
              (CheckedProofV1 ?trace)
              (HistoryConsV1
                (CompletedActionV1 ?action) HistoryNilV1)))))
      (body))
    (rule state-proof-step-result-refused-v1
      (head
        (StateProofStepResultV1
          ?action ?position ?reason ?trace
          (RefusedProofCheckV1 ?refusal-class)
          (HistoryConsV1
            (EnteredActionV1 ?action ?position)
            (HistoryConsV1
              (CheckedProofRefusedV1 ?reason ?trace)
              HistoryNilV1))))
      (body
        (ProofTraceResultIsRefusalV1 ?reason)
        (ProofTraceRefusalClassV1 ?reason ?refusal-class)))
    (rule outcome-is-refusal-proof-check-v1
      (head
        (OutcomeIsRefusalV1 (RefusedProofCheckV1 ?reason)))
      (body))
    (rule outcome-class-proof-check-v1
      (head
        (OutcomeClassV1
          (RefusedProofCheckV1 ?reason) OutcomeRefusedV1))
      (body))

    (rule history-contains-checked-proof-head-v1
      (head
        (HistoryContainsCheckedProofV1
          (HistoryConsV1 (CheckedProofV1 ?trace) ?rest) ?trace))
      (body))
    (rule history-contains-checked-proof-tail-v1
      (head
        (HistoryContainsCheckedProofV1
          (HistoryConsV1 ?event ?rest) ?trace))
      (body
        (HistoryContainsCheckedProofV1 ?rest ?trace)))
  ))
