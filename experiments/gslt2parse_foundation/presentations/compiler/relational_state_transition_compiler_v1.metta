; Lower validated symbolic state actions into the backend-neutral transition
; instruction vocabulary.  Semantic choices live in these rules.  Structural
; identities and numeral reification are explicit inputs produced from the
; source-bound state artifact.

(gslt-presentation-v1 RelationalStateTransitionCompilerV1
  (signature
    ; ---- validated symbolic program
    (operator state-action-v1 3)
    (operator state-final-action-v1 2)
    (operator state-table-v1 4)
    (operator state-require-depth-v1 1)
    (operator state-require-each-v1 3)
    (operator state-require-each-key-absent-v1 3)
    (operator state-insert-unordered-pairs-v1 3)
    (operator state-require-row-v1 2)
    (operator state-require-row-key-absent-v1 2)
    (operator state-insert-row-v1 3)
    (operator state-insert-each-matching-v1 6)
    (operator state-copy-each-row-v1 4)
    (operator state-copy-each-row-matching-v1 6)
    (operator state-check-proof-normal-v1 4)
    (operator state-check-proof-compressed-v1 5)
    (operator state-insert-each-v1 4)
    (operator state-row-cons-v1 2)
    (operator state-literal-v1 1)
    (operator state-role-at-v1 2)
    (operator state-role-list-v1 2)
    (operator state-source-column-v1 1)

    ; ---- generated structural binding inputs
    (operator StateTransitionPackageV1 1)
    (operator StateDenseIdentityV1 4)
    (operator StateNatBindingV1 3)
    (operator StateActionFirstV1 3)
    (operator StateActionNextV1 4)
    (operator StateActionLastV1 3)
    (operator StateFinalActionFirstV1 2)
    (operator StateFinalActionNextV1 3)
    (operator StateFinalActionLastV1 2)
    (operator StateTableFirstV1 2)
    (operator StateTableNextV1 3)
    (operator StateTableLastV1 2)

    ; ---- lowering relations and products
    (operator StateTableLoweringV1 6)
    (operator StateLoweredTableV1 5)
    (operator StateLoweredActionV1 4)
    (operator StateLoweredFinalActionV1 3)
    (operator LowerStateActionV1 3)
    (operator LowerStateRowV1 3)
    (operator LowerStateRowValueV1 3)
    (operator LowerStateWritePolicyV1 2)
    (operator LowerStateLifetimeV1 2)
    (operator ActionsAppendV1 3)
    (operator AssembleStateActionsV1 4)
    (operator AssembleStateFinalActionsV1 3)
    (operator StateOperationProgramV1 3)
    (operator StateFinalProgramV1 2)
    (operator AssembleStateTablesV1 3)
    (operator StateInitialStateV1 2)
    (operator StateTableLifetimeV1 3)

    ; ---- normalized transition vocabulary
    (operator ActionsConsV1 2)
    (operator StateRequireDepthV1 1)
    (operator StateRequireEachV1 3)
    (operator StateRequireEachKeyAbsentV1 4)
    (operator StateInsertAllPairsV1 3)
    (operator StateRequireRowV1 2)
    (operator StateRequireRowKeyAbsentV1 3)
    (operator StateInsertRowV1 4)
    (operator StateInsertEachMatchingV1 7)
    (operator StateCopyEachRowV1 5)
    (operator StateCopyEachRowMatchingV1 7)
    (operator StateCheckProofNormalV1 4)
    (operator StateCheckProofCompressedV1 5)
    (operator StateInsertEachV1 5)
    (operator StateRequirePairwiseDistinctV1 1)
    (operator StateRowConsV1 2)
    (operator StateLiteralSymbolV1 1)
    (operator StateRoleAtV1 2)
    (operator StateRoleListV1 2)
    (operator StateSourceColumnV1 1)
    (operator StateV1 4)
    (operator TableV1 2)
    (operator TablesConsV1 2)
  )
  (equations)
  (rewrites
    ; ===================================================== structural bindings
    (rule lower-state-table-v1
      (head
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime))
      (body
        (StateTransitionPackageV1 ?package)
        (state-table-v1 ?table ?rawArity ?rawKeyArity ?rawLifetime)
        (StateDenseIdentityV1
          ?package StateTableKindV1 ?table ?tableId)
        (StateNatBindingV1 ?package ?rawArity ?arity)
        (StateNatBindingV1 ?package ?rawKeyArity ?keyArity)
        (LowerStateLifetimeV1 ?rawLifetime ?lifetime)))
    (rule project-lowered-state-table-v1
      (head
        (StateLoweredTableV1
          ?package ?tableId ?arity ?keyArity ?lifetime))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)))

    (rule lower-state-lifetime-persistent-v1
      (head
        (LowerStateLifetimeV1
          state-persistent-v1 StatePersistentV1))
      (body))
    (rule lower-state-lifetime-scoped-v1
      (head
        (LowerStateLifetimeV1 state-scoped-v1 StateScopedV1))
      (body))
    (rule lower-state-lifetime-transactional-v1
      (head
        (LowerStateLifetimeV1
          state-transactional-v1 StateTransactionalV1))
      (body))
    (rule project-state-table-lifetime-v1
      (head
        (StateTableLifetimeV1 ?package ?tableId ?lifetime))
      (body
        (StateLoweredTableV1
          ?package ?tableId ?arity ?keyArity ?lifetime)))

    (rule assemble-state-tables-last-v1
      (head
        (AssembleStateTablesV1
          ?package ?tableId
          (TablesConsV1
            (TableV1 ?tableId EntriesNilV1) TablesNilV1)))
      (body
        (StateLoweredTableV1
          ?package ?tableId ?arity ?keyArity ?lifetime)
        (StateTableLastV1 ?package ?tableId)))
    (rule assemble-state-tables-next-v1
      (head
        (AssembleStateTablesV1
          ?package ?tableId
          (TablesConsV1 (TableV1 ?tableId EntriesNilV1) ?rest)))
      (body
        (StateLoweredTableV1
          ?package ?tableId ?arity ?keyArity ?lifetime)
        (StateTableNextV1 ?package ?tableId ?nextTableId)
        (AssembleStateTablesV1 ?package ?nextTableId ?rest)))
    (rule project-state-initial-state-v1
      (head
        (StateInitialStateV1
          ?package
          (StateV1 ?package NatZeroV1 NatZeroV1 ?tables)))
      (body
        (StateTableFirstV1 ?package ?firstTableId)
        (AssembleStateTablesV1 ?package ?firstTableId ?tables)))

    ; ===================================================== row expressions
    (rule lower-state-row-nil-v1
      (head
        (LowerStateRowV1
          ?package state-row-nil-v1 StateRowNilV1))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-row-cons-v1
      (head
        (LowerStateRowV1
          ?package
          (state-row-cons-v1 ?head ?tail)
          (StateRowConsV1 ?lowerHead ?lowerTail)))
      (body
        (LowerStateRowValueV1 ?package ?head ?lowerHead)
        (LowerStateRowV1 ?package ?tail ?lowerTail)))

    (rule lower-state-row-input-v1
      (head
        (LowerStateRowValueV1
          ?package state-input-v1 StateInputV1))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-row-literal-v1
      (head
        (LowerStateRowValueV1
          ?package
          (state-literal-v1 ?literal)
          (StateLiteralSymbolV1 ?literal)))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-row-role-at-v1
      (head
        (LowerStateRowValueV1
          ?package
          (state-role-at-v1 ?role ?rawIndex)
          (StateRoleAtV1 ?role ?index)))
      (body
        (StateNatBindingV1 ?package ?rawIndex ?index)))
    (rule lower-state-row-role-list-v1
      (head
        (LowerStateRowValueV1
          ?package
          (state-role-list-v1 ?role ?rawIndex)
          (StateRoleListV1 ?role ?index)))
      (body
        (StateNatBindingV1 ?package ?rawIndex ?index)))
    (rule lower-state-row-source-column-v1
      (head
        (LowerStateRowValueV1
          ?package
          (state-source-column-v1 ?rawIndex)
          (StateSourceColumnV1 ?index)))
      (body
        (StateNatBindingV1 ?package ?rawIndex ?index)))
    (rule lower-state-row-source-index-v1
      (head
        (LowerStateRowValueV1
          ?package state-source-row-index-v1 StateSourceRowIndexV1))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-row-match-index-v1
      (head
        (LowerStateRowValueV1
          ?package state-source-match-index-v1 StateSourceMatchIndexV1))
      (body
        (StateTransitionPackageV1 ?package)))

    ; ===================================================== policies
    (rule lower-state-write-require-absent-v1
      (head
        (LowerStateWritePolicyV1
          state-require-key-absent-v1 StateRequireKeyAbsentV1))
      (body))
    (rule lower-state-write-or-equal-v1
      (head
        (LowerStateWritePolicyV1
          state-insert-or-require-equal-v1
          StateInsertOrRequireEqualV1))
      (body))
    ; ===================================================== program projections
    (rule lower-state-action-record-v1
      (head
        (StateLoweredActionV1
          ?package ?operation ?index ?actions))
      (body
        (StateTransitionPackageV1 ?package)
        (state-action-v1 ?operation ?index ?action)
        (LowerStateActionV1 ?package ?action ?actions)))
    (rule lower-state-final-action-record-v1
      (head
        (StateLoweredFinalActionV1 ?package ?index ?actions))
      (body
        (StateTransitionPackageV1 ?package)
        (state-final-action-v1 ?index ?action)
        (LowerStateActionV1 ?package ?action ?actions)))

    ; ===================================================== ordered programs
    (rule actions-append-nil-v1
      (head
        (ActionsAppendV1 ActionsNilV1 ?right ?right))
      (body))
    (rule actions-append-cons-v1
      (head
        (ActionsAppendV1
          (ActionsConsV1 ?head ?tail) ?right
          (ActionsConsV1 ?head ?result)))
      (body
        (ActionsAppendV1 ?tail ?right ?result)))

    (rule assemble-state-actions-last-v1
      (head
        (AssembleStateActionsV1
          ?package ?operation ?index ?actions))
      (body
        (StateLoweredActionV1
          ?package ?operation ?index ?actions)
        (StateActionLastV1 ?package ?operation ?index)))
    (rule assemble-state-actions-next-v1
      (head
        (AssembleStateActionsV1
          ?package ?operation ?index ?actions))
      (body
        (StateLoweredActionV1
          ?package ?operation ?index ?current)
        (StateActionNextV1
          ?package ?operation ?index ?nextIndex)
        (AssembleStateActionsV1
          ?package ?operation ?nextIndex ?remaining)
        (ActionsAppendV1 ?current ?remaining ?actions)))
    (rule project-state-operation-program-v1
      (head
        (StateOperationProgramV1 ?package ?operation ?actions))
      (body
        (StateActionFirstV1 ?package ?operation ?firstIndex)
        (AssembleStateActionsV1
          ?package ?operation ?firstIndex ?actions)))

    (rule assemble-state-final-actions-last-v1
      (head
        (AssembleStateFinalActionsV1 ?package ?index ?actions))
      (body
        (StateLoweredFinalActionV1 ?package ?index ?actions)
        (StateFinalActionLastV1 ?package ?index)))
    (rule assemble-state-final-actions-next-v1
      (head
        (AssembleStateFinalActionsV1 ?package ?index ?actions))
      (body
        (StateLoweredFinalActionV1 ?package ?index ?current)
        (StateFinalActionNextV1 ?package ?index ?nextIndex)
        (AssembleStateFinalActionsV1
          ?package ?nextIndex ?remaining)
        (ActionsAppendV1 ?current ?remaining ?actions)))
    (rule project-state-final-program-v1
      (head
        (StateFinalProgramV1 ?package ?actions))
      (body
        (StateFinalActionFirstV1 ?package ?firstIndex)
        (AssembleStateFinalActionsV1
          ?package ?firstIndex ?actions)))

    ; ===================================================== action meanings
    (rule lower-state-require-depth-v1
      (head
        (LowerStateActionV1
          ?package
          (state-require-depth-v1 ?rawDepth)
          (ActionsConsV1
            (StateRequireDepthV1 ?depth) ActionsNilV1)))
      (body
        (StateNatBindingV1 ?package ?rawDepth ?depth)))
    (rule lower-state-push-scope-v1
      (head
        (LowerStateActionV1
          ?package state-push-scope-v1
          (ActionsConsV1 StatePushScopeV1 ActionsNilV1)))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-pop-scope-v1
      (head
        (LowerStateActionV1
          ?package state-pop-scope-v1
          (ActionsConsV1 StatePopScopeV1 ActionsNilV1)))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-noop-v1
      (head
        (LowerStateActionV1
          ?package state-noop-v1
          (ActionsConsV1 StateNoopV1 ActionsNilV1)))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-require-each-v1
      (head
        (LowerStateActionV1
          ?package
          (state-require-each-v1 ?role ?table ?row)
          (ActionsConsV1
            (StateRequireEachV1 ?role ?tableId ?lowerRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateRowV1 ?package ?row ?lowerRow)))
    (rule lower-state-require-each-key-absent-v1
      (head
        (LowerStateActionV1
          ?package
          (state-require-each-key-absent-v1 ?role ?table ?row)
          (ActionsConsV1
            (StateRequireEachKeyAbsentV1
              ?role ?tableId ?keyArity ?lowerRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateRowV1 ?package ?row ?lowerRow)))
    (rule lower-state-insert-unordered-pairs-v1
      (head
        (LowerStateActionV1
          ?package
          (state-insert-unordered-pairs-v1 ?role ?table ?policy)
          (ActionsConsV1
            (StateRequirePairwiseDistinctV1 ?role)
            (ActionsConsV1
              (StateInsertAllPairsV1 ?role ?tableId ?lowerPolicy)
              ActionsNilV1))))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateWritePolicyV1 ?policy ?lowerPolicy)))
    (rule lower-state-require-row-v1
      (head
        (LowerStateActionV1
          ?package
          (state-require-row-v1 ?table ?row)
          (ActionsConsV1
            (StateRequireRowV1 ?tableId ?lowerRow) ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateRowV1 ?package ?row ?lowerRow)))
    (rule lower-state-require-row-key-absent-v1
      (head
        (LowerStateActionV1
          ?package
          (state-require-row-key-absent-v1 ?table ?row)
          (ActionsConsV1
            (StateRequireRowKeyAbsentV1
              ?tableId ?keyArity ?lowerRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateRowV1 ?package ?row ?lowerRow)))
    (rule lower-state-insert-row-v1
      (head
        (LowerStateActionV1
          ?package
          (state-insert-row-v1 ?table ?policy ?row)
          (ActionsConsV1
            (StateInsertRowV1
              ?tableId ?keyArity ?lowerPolicy ?lowerRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateWritePolicyV1 ?policy ?lowerPolicy)
        (LowerStateRowV1 ?package ?row ?lowerRow)))
    (rule lower-state-insert-each-v1
      (head
        (LowerStateActionV1
          ?package
          (state-insert-each-v1 ?role ?table ?policy ?row)
          (ActionsConsV1
            (StateInsertEachV1
              ?role ?tableId ?keyArity ?lowerPolicy ?lowerRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?table ?tableId ?arity ?keyArity ?lifetime)
        (LowerStateWritePolicyV1 ?policy ?lowerPolicy)
        (LowerStateRowV1 ?package ?row ?lowerRow)))
    (rule lower-state-insert-each-matching-v1
      (head
        (LowerStateActionV1
          ?package
          (state-insert-each-matching-v1
            ?role ?conditionTable ?conditionRow
            ?targetTable ?policy ?targetRow)
          (ActionsConsV1
            (StateInsertEachMatchingV1
              ?role ?conditionTableId ?lowerConditionRow
              ?targetTableId ?targetKeyArity ?lowerPolicy ?lowerTargetRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?conditionTable ?conditionTableId
          ?conditionArity ?conditionKeyArity ?conditionLifetime)
        (StateTableLoweringV1
          ?package ?targetTable ?targetTableId
          ?targetArity ?targetKeyArity ?targetLifetime)
        (LowerStateWritePolicyV1 ?policy ?lowerPolicy)
        (LowerStateRowV1
          ?package ?conditionRow ?lowerConditionRow)
        (LowerStateRowV1 ?package ?targetRow ?lowerTargetRow)))
    (rule lower-state-copy-each-row-v1
      (head
        (LowerStateActionV1
          ?package
          (state-copy-each-row-v1
            ?sourceTable ?targetTable ?policy ?targetRow)
          (ActionsConsV1
            (StateCopyEachRowV1
              ?sourceTableId ?targetTableId ?targetKeyArity
              ?lowerPolicy ?lowerTargetRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?sourceTable ?sourceTableId
          ?sourceArity ?sourceKeyArity ?sourceLifetime)
        (StateTableLoweringV1
          ?package ?targetTable ?targetTableId
          ?targetArity ?targetKeyArity ?targetLifetime)
        (LowerStateWritePolicyV1 ?policy ?lowerPolicy)
        (LowerStateRowV1 ?package ?targetRow ?lowerTargetRow)))
    (rule lower-state-copy-each-row-matching-v1
      (head
        (LowerStateActionV1
          ?package
          (state-copy-each-row-matching-v1
            ?sourceTable ?conditionTable ?conditionRow
            ?targetTable ?policy ?targetRow)
          (ActionsConsV1
            (StateCopyEachRowMatchingV1
              ?sourceTableId ?conditionTableId ?lowerConditionRow
              ?targetTableId ?targetKeyArity ?lowerPolicy ?lowerTargetRow)
            ActionsNilV1)))
      (body
        (StateTableLoweringV1
          ?package ?sourceTable ?sourceTableId
          ?sourceArity ?sourceKeyArity ?sourceLifetime)
        (StateTableLoweringV1
          ?package ?conditionTable ?conditionTableId
          ?conditionArity ?conditionKeyArity ?conditionLifetime)
        (StateTableLoweringV1
          ?package ?targetTable ?targetTableId
          ?targetArity ?targetKeyArity ?targetLifetime)
        (LowerStateWritePolicyV1 ?policy ?lowerPolicy)
        (LowerStateRowV1
          ?package ?conditionRow ?lowerConditionRow)
        (LowerStateRowV1 ?package ?targetRow ?lowerTargetRow)))
    (rule lower-state-check-proof-normal-v1
      (head
        (LowerStateActionV1
          ?package
          (state-check-proof-normal-v1
            ?machine ?labelRole ?formulaRole ?stepRole)
          (ActionsConsV1
            (StateCheckProofNormalV1
              ?machine ?labelRole ?formulaRole ?stepRole)
            ActionsNilV1)))
      (body
        (StateTransitionPackageV1 ?package)))
    (rule lower-state-check-proof-compressed-v1
      (head
        (LowerStateActionV1
          ?package
          (state-check-proof-compressed-v1
            ?machine ?labelRole ?formulaRole ?stepRole ?codeRole)
          (ActionsConsV1
            (StateCheckProofCompressedV1
              ?machine ?labelRole ?formulaRole ?stepRole ?codeRole)
            ActionsNilV1)))
      (body
        (StateTransitionPackageV1 ?package)))
  ))
