; What a state action MEANS.  Generic: no guest language appears here.
;
; The state program is a program over action kinds.  Until now the program was
; authored and the action kinds were not, so every backend supplied their meaning
; itself and no source adjudicated between them.  Two backends can disagree
; about a repeated role value -- one treating the values as a list and emitting
; the self-pair, the other treating them as a set and refusing -- and neither can
; be called wrong, because nothing said what the action kind does.  This file is where
; that stops.
;
; ---------------------------------------------------------------- trusted base
;
; Stated precisely, because "the runtime does it" is not a semantic definition:
;
;   1. CONSTRUCTOR TERMS.  Finite first-order terms over the operators declared
;      below, plus the nullary constants they mention.
;   2. FIRST-ORDER MATCHING of a rule head against a ground term, and the
;      resulting substitution.  This is the coGSLT machinery itself.
;   3. Nothing else.  In particular there is no trusted equality primitive, no
;      arithmetic, no negation-as-failure, and no appeal to evaluation order.
;
; Natural numbers, comparison, list operations, table lookup, insertion
; policies and every decision below are AUTHORED here in terms of (1) and (2).
;
; ---------------------------------------------- why values have source identity
;
; A source value is `StateSourceValueV1 identity`, not a raw guest symbol.  The
; boundary chooses a finite canonical identity for each source token.  An
; offline occurrence projection may use a dense natural; the runtime boundary
; may retain an interned atom together with its bytes.  The generic semantics
; observes only the ground identity beneath the source-value constructor.
;
; The boundary owes a faithfulness obligation: equal source tokens must have
; equal identities and distinct source tokens must have distinct identities,
; or the structural comparisons below mean nothing.  It is named
; `state-interning-injective-v1` and it is stated, not assumed silently.
;
; ---------------------------------------------------------- decisions are TOTAL
;
; Every decision relation below returns a RESULT CONSTRUCTOR rather than
; holding or failing to hold.  `NatCompareV1 a b r` always derives exactly one
; r; `SeenLookupV1` answers `SeenAbsentV1` or `SeenAtV1 p`.  This is what makes
; the semantics total on admitted inputs without negation: a rule dispatches on
; a result it is given, and the cases of that result are exhaustive and
; mutually exclusive by construction.
;
; A positive relation that merely fails to hold cannot report a refusal, and a
; refusal approximated by exhausting a search is a statement about the search.
;
; -------------------------------------------------------- what is NOT modelled
;
; RESOURCE FAILURE.  This relation is resource-free by construction: it has no
; allocation and cannot run out of anything.  A backend may answer
; `CheckedExhaustedV1`, and the refinement obligation on a backend is exactly
; that it never answers `Exhausted` where this relation derives an outcome.
; Modelling allocation here would make the DENOTATION depend on the size of the
; machine, which is the confusion the outcome carrier keeps five results apart
; in order to prevent.

(gslt-presentation-v1 RelationalStateTransitionCoreV1
  (signature
    ; ---- naturals and their comparison
    (operator NatSuccV1 1)
    (operator NatCompareV1 3)
    (operator NatAddV1 3)

    ; ---- values, rows, and their decisions
    (operator StateSourceValueV1 1)
    (operator StateLiteralValueV1 1)
    (operator StateListValueV1 1)
    (operator StateIndexValueV1 2)
    (operator StateValueCompareV1 3)
    (operator different 2)
    (operator ValuesConsV1 2)
    (operator StateValueSequenceV1 2)
    (operator StateSymbolSequenceV1 2)
    (operator ValuesSequenceV1 2)
    (operator ValuesTreeLeafV1 1)
    (operator ValuesTreeNodeV1 2)
    (operator ValuesTreeStackConsV1 2)
    (operator ValuesViewV1 2)
    (operator ValuesUnconsViewV1 2)
    (operator ValuesSingletonV1 2)
    (operator ValuesHeadTailV1 3)
    (operator ValuesExactlyTwoV1 3)
    (operator ValueSequenceRealizationV1 2)
    (operator RowConsV1 2)
    (operator RowCompareV1 3)
    (operator RowCompareHeadsV1 4)
    (operator RowKeyPrefixV1 3)

    ; ---- the semantic state
    (operator StateV1 4)
    (operator TableV1 2)
    (operator TablesConsV1 2)
    (operator EntryV1 3)
    (operator EntriesConsV1 2)
    (operator StatePackageOfV1 2)
    (operator StateTableLifetimeV1 3)

    ; ---- observation: what of a state is semantic, and what is index
    (operator StateObservationV1 3)
    (operator TableObservationV1 2)
    (operator ObserveStateV1 2)
    (operator ObserveTablesV1 2)
    (operator ObserveEntriesV1 2)

    ; ---- lookup
    (operator TableOfV1 3)
    (operator TableOfComparedV1 5)
    (operator FoundTableV1 1)
    (operator EntriesLookupKeyV1 4)
    (operator EntriesLookupKeyComparedV1 6)
    (operator FoundRowV1 1)
    (operator EntriesAppendV1 3)
    (operator TablesReplaceV1 4)
    (operator TablesReplaceComparedV1 7)
    (operator PopEntriesV1 3)
    (operator PopEntriesComparedV1 5)
    (operator PopTablesV1 4)
    (operator PopTablesLifetimeV1 7)

    ; ---- insertion
    (operator TableInsertV1 8)
    (operator TableInsertByLookupV1 9)
    (operator TableInsertPresentV1 8)
    (operator TableInsertComparedV1 8)
    (operator InsertedV1 2)
    (operator DuplicateKeyV1 2)

    ; ---- distinctness
    (operator SeenConsV1 3)
    (operator SeenLookupV1 3)
    (operator SeenAtV1 1)
    (operator DistinctScanV1 4)
    (operator DuplicateAtV1 3)
    (operator RequirePairwiseDistinctV1 4)

    ; ---- pair enumeration
    (operator PairsConsV1 2)
    (operator PairV1 2)
    (operator PairsWithV1 3)
    (operator AllPairsV1 2)
    (operator PairsAppendV1 3)

    ; ---- row expressions
    (operator RowExprEvalV1 5)
    (operator EvaluatedRowV1 1)
    (operator ValuesLookupV1 3)
    (operator RowLookupV1 3)
    (operator SourceContextV1 3)

    ; ---- history
    (operator HistoryConsV1 2)
    (operator EnteredActionV1 2)
    (operator CheckedRowPresentV1 2)
    (operator CheckedRowMissingV1 2)
    (operator CheckedKeyAbsentV1 2)
    (operator CheckedPairwiseDistinctV1 1)
    (operator InsertedRowV1 2)
    (operator CompletedActionV1 1)
    (operator CheckedDepthV1 1)
    (operator PushedScopeV1 1)
    (operator PoppedScopeV1 2)
    (operator HistoryAppendV1 3)

    ; ---- the transition
    (operator StateStepV1 7)
    (operator StateRequireDepthComparedV1 7)
    (operator RequireEachV1 7)
    (operator RequireEachByMembershipV1 9)
    (operator RequireEachKeyAbsentV1 8)
    (operator RequireEachKeyAbsentAfterStepV1 11)
    (operator InsertEachV1 10)
    (operator InsertEachAfterStepV1 14)
    (operator InsertEachMatchingV1 12)
    (operator InsertEachMatchingByConditionV1 15)
    (operator InsertEachMatchingAfterInsertV1 17)
    (operator RequireRowV1 5)
    (operator RequireRowByMembershipV1 5)
    (operator RequireRowKeyAbsentV1 6)
    (operator RequireRowKeyAbsentByLookupV1 5)
    (operator InsertRowV1 8)
    (operator InsertRowByTableResultV1 9)
    (operator CopyEachRowV1 11)
    (operator CopyEachRowAfterInsertV1 15)
    (operator CopyEachRowMatchingV1 14)
    (operator CopyEachRowMatchingByConditionV1 17)
    (operator CopyEachRowMatchingAfterInsertV1 19)
    (operator ConditionRowV1 8)
    (operator ConditionRowByMembershipV1 5)
    (operator ConditionMatchedV1 1)
    (operator ConditionMissedV1 1)
    (operator InsertAllPairsV1 7)
    (operator CommitActionStateV1 4)
    (operator CommitActionStateClassifiedV1 4)
    (operator StateInterningInjectiveV1 2)
    (operator OccurrenceInterningFaithfulV1 2)

    ; ---- ordered composition of an operation's actions
    (operator ActionsConsV1 2)
    (operator RunActionsV1 7)
    (operator RunActionsAfterStepV1 11)
    (operator OutcomeClassV1 2)
    (operator OutcomeIsRefusalV1 1)

    (operator StateTablesOfV1 2)
    (operator NamesDifferV1 2)
    (operator RowsConsV1 2)
    (operator EntriesRowMemberV1 3)
    (operator EntriesRowMemberComparedV1 4)
    (operator RowExprValueV1 5)
    (operator StateRowConsV1 2)
    (operator StateLiteralV1 1)
    (operator StateLiteralSymbolV1 1)
    (operator StateRoleAtV1 2)
    (operator StateRoleListV1 2)
    (operator StateSourceColumnV1 1)

    ; ---- the guest-supplied occurrence interface.  These are the ONLY things
    ;      this presentation asks of the layer above it: a role's values, a
    ;      position, and -- for the injectivity obligation alone -- the symbols
    ;      those values were interned from.
    (operator OccurrenceRoleValuesV1 3)
    (operator OccurrencePositionV1 2)
    (operator OccurrenceRoleSymbolsV1 3)
    (operator InterningInjectiveOnV1 2)
    (operator OccurrenceRoleListValueV1 4)
    (operator OccurrenceRoleNonemptyV1 2)
    (operator OccurrenceLiteralValueV1 3)
    (operator OccurrenceStreamOfV1 2)
    (operator StreamLiteralValueV1 3)
    (operator OccurrenceIndexValueV1 3)

    ; ---- the action constructors this slice gives meaning to.  An action kind absent
    ;      from this list derives no outcome, which is a visible gap.
    (operator StateRequireDepthV1 1)
    (operator StateInsertEachV1 5)
    (operator StateRequireEachV1 3)
    (operator StateRequireEachKeyAbsentV1 4)
    (operator StateRequireRowV1 2)
    (operator StateRequireRowKeyAbsentV1 3)
    (operator StateInsertRowV1 4)
    (operator StateInsertEachMatchingV1 7)
    (operator StateCopyEachRowV1 5)
    (operator StateCopyEachRowMatchingV1 7)
    (operator StateCheckProofNormalV1 4)
    (operator StateCheckProofCompressedV1 5)
    (operator StateRequirePairwiseDistinctV1 1)
    (operator StateInsertAllPairsV1 3)

    ; ---- refusal reasons, generic
    (operator RefusedRowAbsentV1 2)
    (operator RefusedDuplicateValueV1 3)
    (operator RefusedKeyDisagreesV1 3)
    (operator RefusedKeyPresentV1 2)
    (operator RefusedUnsupportedRowExprV1 1)
    (operator RefusedDepthMismatchV1 2)
    (operator RefusedScopeUnderflowV1 1)
  )
  (equations)
  (rewrites

    ; ===================================================== naturals
    ;
    ; Total and single-valued: the four rules partition every pair of naturals,
    ; and each recursive case strictly decreases both arguments.
    (rule nat-compare-zero-zero-v1
      (head (NatCompareV1 NatZeroV1 NatZeroV1 CompareEqualV1))
      (body))
    (rule nat-compare-zero-succ-v1
      (head (NatCompareV1 NatZeroV1 (NatSuccV1 ?n) CompareLessV1))
      (body))
    (rule nat-compare-succ-zero-v1
      (head (NatCompareV1 (NatSuccV1 ?n) NatZeroV1 CompareGreaterV1))
      (body))
    (rule nat-compare-succ-succ-v1
      (head (NatCompareV1 (NatSuccV1 ?a) (NatSuccV1 ?b) ?result))
      (body
        (NatCompareV1 ?a ?b ?result)))
    (rule nat-add-zero-v1
      (head (NatAddV1 NatZeroV1 ?right ?right))
      (body))
    (rule nat-add-succ-v1
      (head
        (NatAddV1
          (NatSuccV1 ?left) ?right (NatSuccV1 ?sum)))
      (body
        (NatAddV1 ?left ?right ?sum)))

    ; ===================================================== rows
    ;
    ; A row is a finite list of values.  Values retain their semantic kind;
    ; source interning identifiers, authored literals, list identities, and
    ; occurrence indices do not share an arithmetic namespace.  Ground
    ; equality is structural, while `different` is a target capability whose
    ; realization belongs to the backend composition.
    (rule state-value-compare-equal-v1
      (head (StateValueCompareV1 ?value ?value CompareEqualV1))
      (body))
    (rule state-value-compare-different-v1
      (head
        (StateValueCompareV1 ?left ?right CompareDifferentV1))
      (body
        (different ?left ?right)))

    ; A row is a finite list of values.  Comparison is pointwise and total:
    ; rows of different length are Different, and equal-length rows agree
    ; exactly when every column does.
    (rule row-compare-nil-nil-v1
      (head (RowCompareV1 RowNilV1 RowNilV1 CompareEqualV1))
      (body))
    (rule row-compare-nil-cons-v1
      (head (RowCompareV1 RowNilV1 (RowConsV1 ?v ?rest) CompareDifferentV1))
      (body))
    (rule row-compare-cons-nil-v1
      (head (RowCompareV1 (RowConsV1 ?v ?rest) RowNilV1 CompareDifferentV1))
      (body))
    (rule row-compare-cons-cons-v1
      (head
        (RowCompareV1 (RowConsV1 ?a ?as) (RowConsV1 ?b ?bs) ?result))
      (body
        (StateValueCompareV1 ?a ?b ?comparison)
        (RowCompareHeadsV1 ?comparison ?as ?bs ?result)))
    (rule row-compare-heads-equal-v1
      (head
        (RowCompareHeadsV1 CompareEqualV1 ?as ?bs ?result))
      (body
        (RowCompareV1 ?as ?bs ?result)))
    (rule row-compare-heads-different-v1
      (head
        (RowCompareHeadsV1
          CompareDifferentV1 ?as ?bs CompareDifferentV1))
      (body))

    ; The key of a row is its first k columns.  A row shorter than its declared
    ; key arity has no key, and that is a shape error rather than a lookup
    ; miss -- so it is not given a result here, and the table declaration's
    ; arity obligation is what excludes it.
    (rule row-key-prefix-zero-v1
      (head (RowKeyPrefixV1 NatZeroV1 ?row RowNilV1))
      (body))
    (rule row-key-prefix-succ-v1
      (head
        (RowKeyPrefixV1 (NatSuccV1 ?k) (RowConsV1 ?v ?rest) (RowConsV1 ?v ?key)))
      (body
        (RowKeyPrefixV1 ?k ?rest ?key)))

    ; ===================================================== the state
    ;
    ; (StateV1 package depth seq tables)
    ;
    ;   package  revision identity -- which authored package this state is a
    ;            state OF.  Carried so a state cannot be silently read under a
    ;            different program than it was built by.
    ;   depth    scope depth, a natural.  Depth is semantic: a row's depth
    ;            decides whether leaving a scope removes it.
    ;   seq      the next insertion sequence number.  Order is meaning here --
    ;            declaration order is frame order -- so the counter is part of
    ;            the state and not an implementation convenience.
    ;   tables   a finite list of tables, each a name and its entries.
    ;
    ; An entry is (EntryV1 seq row depth): the row, when it was written, and
    ; the depth it was introduced at.
    ;
    ; ENTRIES ARE OLDEST-FIRST.  Insertion appends.  This is a semantic
    ; decision, not a storage one: a backend may store them however it likes,
    ; but its observation must agree with this order.

    (rule table-of-nil-v1
      (head (TableOfV1 TablesNilV1 ?name TableAbsentV1))
      (body))
    (rule table-of-cons-v1
      (head
        (TableOfV1
          (TablesConsV1 (TableV1 ?other ?entries) ?rest)
          ?name ?result))
      (body
        (StateValueCompareV1 ?other ?name ?comparison)
        (TableOfComparedV1
          ?comparison ?entries ?rest ?name ?result)))
    (rule table-of-compared-equal-v1
      (head
        (TableOfComparedV1
          CompareEqualV1 ?entries ?rest ?name
          (FoundTableV1 ?entries)))
      (body))
    (rule table-of-compared-different-v1
      (head
        (TableOfComparedV1
          CompareDifferentV1 ?entries ?rest ?name ?result))
      (body
        (TableOfV1 ?rest ?name ?result)))

    ; Table names are values too, so their derived disequality guard uses the
    ; same total comparison as table lookup and replacement.  Lookup dispatches
    ; directly on that comparison above, making equal and unequal continuations
    ; structurally distinct instead of overlapping hit/miss alternatives.
    (rule names-differ-v1
      (head (NamesDifferV1 ?a ?b))
      (body
        (StateValueCompareV1 ?a ?b CompareDifferentV1)))

    ; Lookup BY KEY, not by row.  These are different questions and conflating
    ; them is a live defect class: a table whose key arity is smaller than its
    ; row arity will answer "present" for a row it does not contain.
    (rule entries-lookup-key-nil-v1
      (head (EntriesLookupKeyV1 EntriesNilV1 ?keyArity ?key RowAbsentV1))
      (body))
    (rule entries-lookup-key-cons-v1
      (head
        (EntriesLookupKeyV1
          (EntriesConsV1 (EntryV1 ?seq ?row ?depth) ?rest)
          ?keyArity ?key ?result))
      (body
        (RowKeyPrefixV1 ?keyArity ?row ?rowKey)
        (RowCompareV1 ?rowKey ?key ?comparison)
        (EntriesLookupKeyComparedV1
          ?comparison ?row ?rest ?keyArity ?key ?result)))
    (rule entries-lookup-key-compared-equal-v1
      (head
        (EntriesLookupKeyComparedV1
          CompareEqualV1 ?row ?rest ?keyArity ?key
          (FoundRowV1 ?row)))
      (body))
    (rule entries-lookup-key-compared-different-v1
      (head
        (EntriesLookupKeyComparedV1
          CompareDifferentV1 ?row ?rest ?keyArity ?key ?result))
      (body
        (EntriesLookupKeyV1 ?rest ?keyArity ?key ?result)))

    (rule entries-append-nil-v1
      (head (EntriesAppendV1 EntriesNilV1 ?entry (EntriesConsV1 ?entry EntriesNilV1)))
      (body))
    (rule entries-append-cons-v1
      (head
        (EntriesAppendV1 (EntriesConsV1 ?head ?rest) ?entry
                         (EntriesConsV1 ?head ?out)))
      (body
        (EntriesAppendV1 ?rest ?entry ?out)))

    (rule tables-replace-cons-v1
      (head
        (TablesReplaceV1
          (TablesConsV1 (TableV1 ?other ?entries) ?rest)
          ?name ?new ?out))
      (body
        (StateValueCompareV1 ?other ?name ?comparison)
        (TablesReplaceComparedV1
          ?comparison ?other ?entries ?rest ?name ?new ?out)))
    (rule tables-replace-compared-equal-v1
      (head
        (TablesReplaceComparedV1
          CompareEqualV1 ?other ?entries ?rest ?name ?new
          (TablesConsV1 (TableV1 ?name ?new) ?rest)))
      (body))
    (rule tables-replace-compared-different-v1
      (head
        (TablesReplaceComparedV1
          CompareDifferentV1 ?other ?entries ?rest ?name ?new
          (TablesConsV1 (TableV1 ?other ?entries) ?out)))
      (body
        (TablesReplaceV1 ?rest ?name ?new ?out)))

    ; ===================================================== observation
    ;
    ; What of a state is SEMANTIC and what is bookkeeping.  A backend is free
    ; to hold an index, a hash, a cursor or a cache; none of that is state.
    ; Two states are semantically equal exactly when their observations are.
    ;
    ; The observation keeps package identity, depth, and each table's rows IN
    ; ORDER, and drops the sequence counter -- order is already carried by the
    ; list, so keeping the counter too would make two states that behave
    ; identically compare unequal merely because they were reached by different
    ; routes.
    (rule observe-state-v1
      (head
        (ObserveStateV1
          (StateV1 ?package ?depth ?seq ?tables)
          (StateObservationV1 ?package ?depth ?observed)))
      (body
        (ObserveTablesV1 ?tables ?observed)))
    (rule observe-tables-nil-v1
      (head (ObserveTablesV1 TablesNilV1 TablesNilV1))
      (body))
    (rule observe-tables-cons-v1
      (head
        (ObserveTablesV1
          (TablesConsV1 (TableV1 ?name ?entries) ?rest)
          (TablesConsV1 (TableObservationV1 ?name ?rows) ?out)))
      (body
        (ObserveEntriesV1 ?entries ?rows)
        (ObserveTablesV1 ?rest ?out)))
    (rule observe-entries-nil-v1
      (head (ObserveEntriesV1 EntriesNilV1 RowsNilV1))
      (body))
    (rule observe-entries-cons-v1
      (head
        (ObserveEntriesV1
          (EntriesConsV1 (EntryV1 ?seq ?row ?depth) ?rest)
          (RowsConsV1 ?row ?out)))
      (body
        (ObserveEntriesV1 ?rest ?out)))

    ; ===================================================== insertion
    ;
    ; (TableInsertV1 state name keyArity policy row outcome afterState history)
    ;
    ; Four cases, exhaustive and mutually exclusive:
    ;
    ;   key absent                              -> insert, sequence advances
    ;   key present, row equal, or-require-equal -> accept, state unchanged
    ;   key present, row differs, or-require-equal -> refuse, keys disagree
    ;   key present, require-key-absent          -> refuse, key already present
    ;
    ; The second case is why this is not simply "insert if absent": a program
    ; may legitimately restate a row it already established, and restating is
    ; not the same event as contradicting.
    (rule table-insert-v1
      (head
        (TableInsertV1
          ?state
          ?name ?keyArity ?policy ?row
          ?outcome ?after ?history))
      (body
        (StateTablesOfV1 ?state ?tables)
        (TableOfV1 ?tables ?name (FoundTableV1 ?entries))
        (RowKeyPrefixV1 ?keyArity ?row ?key)
        (EntriesLookupKeyV1 ?entries ?keyArity ?key ?lookup)
        (TableInsertByLookupV1
          ?lookup ?entries ?state ?name ?policy ?row
          ?outcome ?after ?history)))

    (rule table-insert-by-lookup-absent-v1
      (head
        (TableInsertByLookupV1
          RowAbsentV1 ?entries
          (StateV1 ?package ?depth ?seq ?tables)
          ?name ?policy ?row
          (InsertedV1 ?name ?row)
          (StateV1 ?package ?depth (NatSuccV1 ?seq) ?tablesOut)
          (InsertedRowV1 ?name ?row)))
      (body
        (EntriesAppendV1 ?entries (EntryV1 ?seq ?row ?depth) ?entriesOut)
        (TablesReplaceV1 ?tables ?name ?entriesOut ?tablesOut)))

    (rule table-insert-by-lookup-present-v1
      (head
        (TableInsertByLookupV1
          (FoundRowV1 ?existing) ?entries ?state ?name ?policy ?row
          ?outcome ?after ?history))
      (body
        (TableInsertPresentV1
          ?policy ?existing ?state ?name ?row
          ?outcome ?after ?history)))

    (rule table-insert-present-or-require-equal-v1
      (head
        (TableInsertPresentV1
          StateInsertOrRequireEqualV1 ?existing ?state ?name ?row
          ?outcome ?after ?history))
      (body
        (RowCompareV1 ?existing ?row ?comparison)
        (TableInsertComparedV1
          ?comparison ?existing ?state ?name ?row
          ?outcome ?after ?history)))

    (rule table-insert-present-required-absent-v1
      (head
        (TableInsertPresentV1
          StateRequireKeyAbsentV1 ?existing ?state ?name ?row
          (RefusedKeyPresentV1 ?name ?existing) ?state
          (CheckedRowPresentV1 ?name ?existing)))
      (body))

    (rule table-insert-compared-equal-v1
      (head
        (TableInsertComparedV1
          CompareEqualV1 ?existing ?state ?name ?row
          (InsertedV1 ?name ?row) ?state
          (CheckedRowPresentV1 ?name ?row)))
      (body))

    (rule table-insert-compared-different-v1
      (head
        (TableInsertComparedV1
          CompareDifferentV1 ?existing ?state ?name ?row
          (RefusedKeyDisagreesV1 ?name ?row ?existing) ?state
          (CheckedRowPresentV1 ?name ?existing)))
      (body))

    (rule state-tables-of-v1
      (head (StateTablesOfV1 (StateV1 ?package ?depth ?seq ?tables) ?tables))
      (body))
    (rule state-package-of-v1
      (head
        (StatePackageOfV1
          (StateV1 ?package ?depth ?seq ?tables) ?package))
      (body))

    ; A value sequence is consumed through its left view rather than by
    ; assuming one concrete representation.  Authored canaries may retain the
    ; ordinary cons form; generated occurrence streams use a balanced tree and
    ; a logarithmic traversal stack.  The semantic folds below are identical
    ; for both realizations.
    (rule values-view-list-empty-v1
      (head (ValuesViewV1 ValuesNilV1 ValuesEmptyViewV1))
      (body))
    (rule values-view-list-uncons-v1
      (head
        (ValuesViewV1
          (ValuesConsV1 ?value ?rest)
          (ValuesUnconsViewV1 ?value ?rest)))
      (body))
    (rule values-view-realized-v1
      (head
        (ValuesViewV1
          (StateValueSequenceV1 ?stream ?sequenceId) ?view))
      (body
        (ValueSequenceRealizationV1
          (StateValueSequenceV1 ?stream ?sequenceId) ?sequence)
        (ValuesViewV1 ?sequence ?view)))
    (rule values-view-tree-empty-v1
      (head
        (ValuesViewV1
          (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1)
          ValuesEmptyViewV1))
      (body))
    (rule values-view-tree-pop-v1
      (head
        (ValuesViewV1
          (ValuesSequenceV1 ValuesTreeEmptyV1
            (ValuesTreeStackConsV1 ?tree ?stack))
          ?view))
      (body
        (ValuesViewV1 (ValuesSequenceV1 ?tree ?stack) ?view)))
    (rule values-view-tree-leaf-v1
      (head
        (ValuesViewV1
          (ValuesSequenceV1 (ValuesTreeLeafV1 ?value) ?stack)
          (ValuesUnconsViewV1 ?value
            (ValuesSequenceV1 ValuesTreeEmptyV1 ?stack))))
      (body))
    (rule values-view-tree-node-v1
      (head
        (ValuesViewV1
          (ValuesSequenceV1 (ValuesTreeNodeV1 ?left ?right) ?stack)
          ?view))
      (body
        (ValuesViewV1
          (ValuesSequenceV1 ?left
            (ValuesTreeStackConsV1 ?right ?stack))
          ?view)))

    (rule values-singleton-v1
      (head (ValuesSingletonV1 ?values ?value))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (ValuesViewV1 ?rest ValuesEmptyViewV1)))
    (rule values-head-tail-v1
      (head (ValuesHeadTailV1 ?values ?head ?tail))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?head ?tail))))
    (rule values-exactly-two-v1
      (head (ValuesExactlyTwoV1 ?values ?first ?second))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?first ?rest))
        (ValuesViewV1 ?rest (ValuesUnconsViewV1 ?second ?end))
        (ValuesViewV1 ?end ValuesEmptyViewV1)))

    (rule occurrence-role-nonempty-v1
      (head
        (OccurrenceRoleNonemptyV1 ?occurrence ?role))
      (body
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (ValuesViewV1
          ?values (ValuesUnconsViewV1 ?value ?rest))))

    ; ===================================================== scope projection
    ;
    ; Scoped rows written at the current depth disappear when that depth is
    ; popped. Persistent and transactional tables are unchanged by a scope
    ; pop; transaction cleanup is a separate operation-boundary transition.
    (rule pop-entries-nil-v1
      (head (PopEntriesV1 EntriesNilV1 ?depth EntriesNilV1))
      (body))
    (rule pop-entries-cons-v1
      (head
        (PopEntriesV1
          (EntriesConsV1 (EntryV1 ?seq ?row ?entryDepth) ?rest)
          ?depth ?out))
      (body
        (NatCompareV1 ?entryDepth ?depth ?comparison)
        (PopEntriesComparedV1
          ?comparison (EntryV1 ?seq ?row ?entryDepth) ?rest ?depth ?out)))
    (rule pop-entries-compared-keep-v1
      (head
        (PopEntriesComparedV1
          CompareLessV1 (EntryV1 ?seq ?row ?entryDepth) ?rest ?depth
          (EntriesConsV1 (EntryV1 ?seq ?row ?entryDepth) ?out)))
      (body
        (PopEntriesV1 ?rest ?depth ?out)))
    (rule pop-entries-compared-drop-v1
      (head
        (PopEntriesComparedV1
          CompareEqualV1 ?entry ?rest ?depth ?out))
      (body
        (PopEntriesV1 ?rest ?depth ?out)))

    (rule pop-tables-nil-v1
      (head
        (PopTablesV1 ?package TablesNilV1 ?depth TablesNilV1))
      (body))
    (rule pop-tables-cons-v1
      (head
        (PopTablesV1
          ?package
          (TablesConsV1 (TableV1 ?name ?entries) ?rest)
          ?depth ?out))
      (body
        (StateTableLifetimeV1 ?package ?name ?lifetime)
        (PopTablesLifetimeV1
          ?lifetime ?package ?name ?entries ?rest ?depth ?out)))
    (rule pop-tables-lifetime-persistent-v1
      (head
        (PopTablesLifetimeV1
          StatePersistentV1 ?package ?name ?entries ?rest ?depth
          (TablesConsV1 (TableV1 ?name ?entries) ?out)))
      (body
        (PopTablesV1 ?package ?rest ?depth ?out)))
    (rule pop-tables-lifetime-transactional-v1
      (head
        (PopTablesLifetimeV1
          StateTransactionalV1 ?package ?name ?entries ?rest ?depth
          (TablesConsV1 (TableV1 ?name ?entries) ?out)))
      (body
        (PopTablesV1 ?package ?rest ?depth ?out)))
    (rule pop-tables-lifetime-scoped-v1
      (head
        (PopTablesLifetimeV1
          StateScopedV1 ?package ?name ?entries ?rest
          ?depth
          (TablesConsV1 (TableV1 ?name ?kept) ?out)))
      (body
        (PopEntriesV1 ?entries ?depth ?kept)
        (PopTablesV1 ?package ?rest ?depth ?out)))

    ; ===================================================== distinctness
    ;
    ; A guest may require the values of one role to be pairwise distinct.  This
    ; relation decides that requirement and reports
    ; WHICH value repeated and at which two positions -- because a refusal a
    ; reader cannot locate in the source is not evidence about the source.
    ;
    ; `seen` carries each value with the position it first appeared at, so the
    ; witness names the first occurrence and the current one rather than "some
    ; duplicate somewhere".
    (rule seen-lookup-nil-v1
      (head (SeenLookupV1 SeenNilV1 ?value SeenAbsentV1))
      (body))
    (rule seen-lookup-hit-v1
      (head
        (SeenLookupV1 (SeenConsV1 ?other ?position ?rest) ?value
                      (SeenAtV1 ?position)))
      (body
        (StateValueCompareV1 ?value ?other CompareEqualV1)))
    (rule seen-lookup-miss-v1
      (head
        (SeenLookupV1 (SeenConsV1 ?other ?position ?rest) ?value ?result))
      (body
        (StateValueCompareV1 ?value ?other CompareDifferentV1)
        (SeenLookupV1 ?rest ?value ?result)))

    (rule distinct-scan-nil-v1
      (head (DistinctScanV1 ?values ?index ?seen DistinctOkV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule distinct-scan-duplicate-v1
      (head
        (DistinctScanV1 ?values ?index ?seen
                        (DuplicateAtV1 ?value ?first ?index)))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (SeenLookupV1 ?seen ?value (SeenAtV1 ?first))))
    (rule distinct-scan-fresh-v1
      (head
        (DistinctScanV1 ?values ?index ?seen ?result))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (SeenLookupV1 ?seen ?value SeenAbsentV1)
        (DistinctScanV1 ?rest (NatSuccV1 ?index)
                        (SeenConsV1 ?value ?index ?seen) ?result)))

    ; ===================================================== pair enumeration
    ;
    ; Exactly the pairs (v_i, v_j) with i < j, in the order: the head paired
    ; with each later value in order, then recursively on the tail.
    ;
    ; Because this runs AFTER distinctness, its domain is a list with no
    ; repeats, so it has no opinion left to hold about self-pairs -- it cannot
    ; produce one.  That is the whole reason the fused action kind was split: a
    ; construction with a clean domain cannot silently decide a question that
    ; belongs to the guest.
    (rule pairs-with-nil-v1
      (head (PairsWithV1 ?value ?values PairsNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule pairs-with-cons-v1
      (head
        (PairsWithV1 ?value ?values
                     (PairsConsV1 (PairV1 ?value ?other) ?out)))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?other ?rest))
        (PairsWithV1 ?value ?rest ?out)))

    (rule pairs-append-nil-v1
      (head (PairsAppendV1 PairsNilV1 ?ys ?ys))
      (body))
    (rule pairs-append-cons-v1
      (head
        (PairsAppendV1 (PairsConsV1 ?p ?xs) ?ys (PairsConsV1 ?p ?out)))
      (body
        (PairsAppendV1 ?xs ?ys ?out)))

    (rule all-pairs-nil-v1
      (head (AllPairsV1 ?values PairsNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule all-pairs-cons-v1
      (head (AllPairsV1 ?values ?out))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (PairsWithV1 ?value ?rest ?head)
        (AllPairsV1 ?rest ?tail)
        (PairsAppendV1 ?head ?tail ?out)))

    ; ===================================================== row expressions
    ;
    ; The forms this slice needs, and NOTHING ELSE.  An unrecognised form
    ; answers `RefusedUnsupportedRowExprV1`, which is an Unsupported result
    ; about this backend and never a Rejected one about the input.  Adding a
    ; form means authoring its rule here; there is no default.
    (rule values-lookup-zero-v1
      (head
        (ValuesLookupV1 ?values NatZeroV1 ?value))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))))
    (rule values-lookup-succ-v1
      (head
        (ValuesLookupV1 ?values (NatSuccV1 ?index) ?value))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?head ?rest))
        (ValuesLookupV1 ?rest ?index ?value)))
    (rule row-lookup-zero-v1
      (head
        (RowLookupV1
          (RowConsV1 ?value ?rest) NatZeroV1 ?value))
      (body))
    (rule row-lookup-succ-v1
      (head
        (RowLookupV1
          (RowConsV1 ?head ?rest) (NatSuccV1 ?index) ?value))
      (body
        (RowLookupV1 ?rest ?index ?value)))

    (rule row-expr-eval-nil-v1
      (head
        (RowExprEvalV1
          StateRowNilV1 ?occurrence ?input ?source
          (EvaluatedRowV1 RowNilV1)))
      (body))
    (rule row-expr-eval-cons-v1
      (head
        (RowExprEvalV1
          (StateRowConsV1 ?head ?tail) ?occurrence ?input ?source
          (EvaluatedRowV1 (RowConsV1 ?value ?rest))))
      (body
        (RowExprValueV1
          ?head ?occurrence ?input ?source ?value)
        (RowExprEvalV1
          ?tail ?occurrence ?input ?source (EvaluatedRowV1 ?rest))))
    (rule row-expr-value-input-v1
      (head
        (RowExprValueV1
          StateInputV1 ?occurrence ?input ?source ?input))
      (body))
    (rule row-expr-value-literal-v1
      (head
        (RowExprValueV1
          (StateLiteralV1 ?value) ?occurrence ?input ?source ?value))
      (body))
    (rule row-expr-value-literal-symbol-v1
      (head
        (RowExprValueV1
          (StateLiteralSymbolV1 ?literal)
          ?occurrence ?input ?source ?value))
      (body
        (OccurrenceLiteralValueV1
          ?occurrence ?literal ?value)))
    (rule occurrence-literal-value-from-stream-v1
      (head
        (OccurrenceLiteralValueV1
          ?occurrence ?literal ?value))
      (body
        (OccurrenceStreamOfV1 ?occurrence ?stream)
        (StreamLiteralValueV1 ?stream ?literal ?value)))
    (rule row-expr-value-role-at-v1
      (head
        (RowExprValueV1
          (StateRoleAtV1 ?role ?index)
          ?occurrence ?input ?source ?value))
      (body
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (ValuesLookupV1 ?values ?index ?value)))
    (rule row-expr-value-role-list-v1
      (head
        (RowExprValueV1
          (StateRoleListV1 ?role ?index)
          ?occurrence ?input ?source ?value))
      (body
        (OccurrenceRoleListValueV1
          ?occurrence ?role ?index ?value)))
    (rule row-expr-value-source-column-v1
      (head
        (RowExprValueV1
          (StateSourceColumnV1 ?index)
          ?occurrence ?input
          (SourceContextV1 ?row ?rowIndex ?matchIndex)
          ?value))
      (body
        (RowLookupV1 ?row ?index ?value)))
    (rule row-expr-value-source-row-index-v1
      (head
        (RowExprValueV1
          StateSourceRowIndexV1
          ?occurrence ?input
          (SourceContextV1 ?row ?rowIndex ?matchIndex)
          ?value))
      (body
        (OccurrenceIndexValueV1
          ?occurrence ?rowIndex ?value)))
    (rule row-expr-value-source-match-index-v1
      (head
        (RowExprValueV1
          StateSourceMatchIndexV1
          ?occurrence ?input
          (SourceContextV1 ?row ?rowIndex ?matchIndex)
          ?value))
      (body
        (OccurrenceIndexValueV1
          ?occurrence ?matchIndex ?value)))
    (rule occurrence-index-value-from-stream-v1
      (head
        (OccurrenceIndexValueV1
          ?occurrence ?index
          (StateIndexValueV1 ?occurrence ?index)))
      (body))
    ; A mutating action computes a tentative state.  Acceptance commits it;
    ; every authored refusal restores the state offered to the action.  The
    ; attempted history remains observable, but no partial state escapes.
    (rule commit-action-state-v1
      (head
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after))
      (body
        (OutcomeClassV1 ?outcome ?class)
        (CommitActionStateClassifiedV1
          ?class ?before ?tentative ?after)))
    (rule commit-action-state-classified-accepted-v1
      (head
        (CommitActionStateClassifiedV1
          OutcomeAcceptedV1 ?before ?tentative ?tentative))
      (body))
    (rule commit-action-state-classified-refused-v1
      (head
        (CommitActionStateClassifiedV1
          OutcomeRefusedV1 ?before ?tentative ?before))
      (body))

    ; ===================================================== the transition
    ;
    ; (StateStepV1 package before occurrence action outcome after history)
    ;
    ; The occurrence carries the position, so every outcome below can say where
    ; in the source it happened.
    ;
    ; The action kinds below are interpreted without a default case.  Every
    ; absent action kind is deliberately absent -- an absent action kind
    ; derives no outcome at all, which is a visible gap, whereas a default case
    ; would be an invisible guess.

    ; ---- scope control and no-op
    (rule state-step-require-depth-v1
      (head
        (StateStepV1
          ?package
          (StateV1 ?package ?depth ?seq ?tables)
          ?occurrence
          (StateRequireDepthV1 ?expected)
          ?outcome ?after ?history))
      (body
        (OccurrencePositionV1 ?occurrence ?position)
        (NatCompareV1 ?depth ?expected ?comparison)
        (StateRequireDepthComparedV1
          ?comparison
          (StateV1 ?package ?depth ?seq ?tables)
          (StateRequireDepthV1 ?expected)
          ?position
          ?outcome ?after ?history)))

    (rule state-require-depth-compared-equal-v1
      (head
        (StateRequireDepthComparedV1
          CompareEqualV1
          (StateV1 ?package ?depth ?seq ?tables)
          (StateRequireDepthV1 ?expected)
          ?position AcceptedV1
          (StateV1 ?package ?depth ?seq ?tables)
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireDepthV1 ?expected) ?position)
            (HistoryConsV1 (CheckedDepthV1 ?depth) HistoryNilV1))))
      (body))

    (rule state-require-depth-compared-less-v1
      (head
        (StateRequireDepthComparedV1
          CompareLessV1
          (StateV1 ?package ?depth ?seq ?tables)
          (StateRequireDepthV1 ?expected)
          ?position (RefusedDepthMismatchV1 ?expected ?depth)
          (StateV1 ?package ?depth ?seq ?tables)
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireDepthV1 ?expected) ?position)
            HistoryNilV1)))
      (body))

    (rule state-require-depth-compared-greater-v1
      (head
        (StateRequireDepthComparedV1
          CompareGreaterV1
          (StateV1 ?package ?depth ?seq ?tables)
          (StateRequireDepthV1 ?expected)
          ?position (RefusedDepthMismatchV1 ?expected ?depth)
          (StateV1 ?package ?depth ?seq ?tables)
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireDepthV1 ?expected) ?position)
            HistoryNilV1)))
      (body))

    (rule state-step-push-scope-v1
      (head
        (StateStepV1
          ?package
          (StateV1 ?package ?depth ?seq ?tables)
          ?occurrence StatePushScopeV1 AcceptedV1
          (StateV1 ?package (NatSuccV1 ?depth) ?seq ?tables)
          (HistoryConsV1
            (EnteredActionV1 StatePushScopeV1 ?position)
            (HistoryConsV1
              (PushedScopeV1 (NatSuccV1 ?depth)) HistoryNilV1))))
      (body
        (OccurrencePositionV1 ?occurrence ?position)))

    (rule state-step-pop-scope-v1
      (head
        (StateStepV1
          ?package
          (StateV1 ?package (NatSuccV1 ?depth) ?seq ?tables)
          ?occurrence StatePopScopeV1 AcceptedV1
          (StateV1 ?package ?depth ?seq ?out)
          (HistoryConsV1
            (EnteredActionV1 StatePopScopeV1 ?position)
            (HistoryConsV1
              (PoppedScopeV1 (NatSuccV1 ?depth) ?depth) HistoryNilV1))))
      (body
        (OccurrencePositionV1 ?occurrence ?position)
        (PopTablesV1 ?package ?tables (NatSuccV1 ?depth) ?out)))
    (rule state-step-pop-scope-underflow-v1
      (head
        (StateStepV1
          ?package
          (StateV1 ?package NatZeroV1 ?seq ?tables)
          ?occurrence StatePopScopeV1
          (RefusedScopeUnderflowV1 NatZeroV1)
          (StateV1 ?package NatZeroV1 ?seq ?tables)
          (HistoryConsV1
            (EnteredActionV1 StatePopScopeV1 ?position)
            HistoryNilV1)))
      (body
        (OccurrencePositionV1 ?occurrence ?position)))

    (rule state-step-noop-v1
      (head
        (StateStepV1
          ?package ?state ?occurrence StateNoopV1
          AcceptedV1 ?state
          (HistoryConsV1
            (EnteredActionV1 StateNoopV1 ?position)
            (HistoryConsV1
              (CompletedActionV1 StateNoopV1) HistoryNilV1))))
      (body
        (StatePackageOfV1 ?state ?package)
        (OccurrencePositionV1 ?occurrence ?position)))

    ; ---- require every value of a role to be present as a row in a table
    (rule state-step-require-each-v1
      (head
        (StateStepV1 ?package ?before ?occurrence
                     (StateRequireEachV1 ?role ?table ?rowExpr)
                     ?outcome ?before ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (RequireEachV1
          ?before ?occurrence ?table ?rowExpr ?values ?outcome ?steps)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireEachV1 ?role ?table ?rowExpr) ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule require-each-nil-v1
      (head
        (RequireEachV1 ?state ?occurrence ?table ?rowExpr ?values
                       AcceptedV1 HistoryNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule require-each-cons-v1
      (head
        (RequireEachV1
          ?state ?occurrence ?table ?rowExpr
          ?values
          ?outcome ?history))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (RowExprEvalV1
          ?rowExpr ?occurrence ?value NoSourceContextV1
          (EvaluatedRowV1 ?row))
        (StateTablesOfV1 ?state ?tables)
        (TableOfV1 ?tables ?table (FoundTableV1 ?entries))
        (EntriesRowMemberV1 ?entries ?row ?membership)
        (RequireEachByMembershipV1
          ?membership ?state ?occurrence ?table ?rowExpr ?row ?rest
          ?outcome ?history)))
    (rule require-each-by-membership-present-v1
      (head
        (RequireEachByMembershipV1
          RowPresentV1 ?state ?occurrence ?table ?rowExpr ?row ?rest
          ?outcome
          (HistoryConsV1 (CheckedRowPresentV1 ?table ?row) ?steps)))
      (body
        (RequireEachV1
          ?state ?occurrence ?table ?rowExpr ?rest ?outcome ?steps)))
    (rule require-each-by-membership-missing-v1
      (head
        (RequireEachByMembershipV1
          RowMissingV1 ?state ?occurrence ?table ?rowExpr ?row ?rest
          (RefusedRowAbsentV1 ?table ?row) HistoryNilV1))
      (body))

    ; Membership is by EXACT ROW, and is a different question from lookup by
    ; key.  Keeping them apart is not pedantry: a table with key arity smaller
    ; than its row arity answers the two questions differently, and using the
    ; key one here made every constant count as a variable in an earlier
    ; backend.
    (rule entries-row-member-nil-v1
      (head (EntriesRowMemberV1 EntriesNilV1 ?row RowMissingV1))
      (body))
    (rule entries-row-member-cons-v1
      (head
        (EntriesRowMemberV1 (EntriesConsV1 (EntryV1 ?s ?other ?d) ?rest) ?row
                            ?result))
      (body
        (RowCompareV1 ?other ?row ?comparison)
        (EntriesRowMemberComparedV1
          ?comparison ?rest ?row ?result)))
    (rule entries-row-member-compared-equal-v1
      (head
        (EntriesRowMemberComparedV1
          CompareEqualV1 ?rest ?row RowPresentV1))
      (body))
    (rule entries-row-member-compared-different-v1
      (head
        (EntriesRowMemberComparedV1
          CompareDifferentV1 ?rest ?row ?result))
      (body
        (EntriesRowMemberV1 ?rest ?row ?result)))

    ; A filter is an exact-row membership decision.  Both branches carry the
    ; evaluated row in their result and history, so skipping an input is an
    ; observable semantic decision rather than silent control flow.
    (rule condition-row-v1
      (head
        (ConditionRowV1
          ?state ?occurrence ?input ?source ?table ?rowExpr
          ?result ?history))
      (body
        (RowExprEvalV1
          ?rowExpr ?occurrence ?input ?source (EvaluatedRowV1 ?row))
        (StateTablesOfV1 ?state ?tables)
        (TableOfV1 ?tables ?table (FoundTableV1 ?entries))
        (EntriesRowMemberV1 ?entries ?row ?membership)
        (ConditionRowByMembershipV1
          ?membership ?table ?row ?result ?history)))
    (rule condition-row-by-membership-present-v1
      (head
        (ConditionRowByMembershipV1
          RowPresentV1 ?table ?row
          (ConditionMatchedV1 ?row)
          (CheckedRowPresentV1 ?table ?row)))
      (body))
    (rule condition-row-by-membership-missing-v1
      (head
        (ConditionRowByMembershipV1
          RowMissingV1 ?table ?row
          (ConditionMissedV1 ?row)
          (CheckedRowMissingV1 ?table ?row)))
      (body))

    ; ---- key absence, insertion, and direct rows
    (rule require-row-v1
      (head
        (RequireRowV1
          ?state ?table ?row ?outcome ?history))
      (body
        (StateTablesOfV1 ?state ?tables)
        (TableOfV1 ?tables ?table (FoundTableV1 ?entries))
        (EntriesRowMemberV1 ?entries ?row ?membership)
        (RequireRowByMembershipV1
          ?membership ?table ?row ?outcome ?history)))
    (rule require-row-by-membership-present-v1
      (head
        (RequireRowByMembershipV1
          RowPresentV1 ?table ?row AcceptedV1
          (HistoryConsV1
            (CheckedRowPresentV1 ?table ?row) HistoryNilV1)))
      (body))
    (rule require-row-by-membership-absent-v1
      (head
        (RequireRowByMembershipV1
          RowMissingV1 ?table ?row
          (RefusedRowAbsentV1 ?table ?row) HistoryNilV1))
      (body))

    (rule require-row-key-absent-v1
      (head
        (RequireRowKeyAbsentV1
          ?state ?table ?keyArity ?row ?outcome ?history))
      (body
        (StateTablesOfV1 ?state ?tables)
        (TableOfV1 ?tables ?table (FoundTableV1 ?entries))
        (RowKeyPrefixV1 ?keyArity ?row ?key)
        (EntriesLookupKeyV1
          ?entries ?keyArity ?key ?lookup)
        (RequireRowKeyAbsentByLookupV1
          ?lookup ?table ?key ?outcome ?history)))
    (rule require-row-key-absent-by-lookup-absent-v1
      (head
        (RequireRowKeyAbsentByLookupV1
          RowAbsentV1 ?table ?key AcceptedV1
          (HistoryConsV1
            (CheckedKeyAbsentV1 ?table ?key) HistoryNilV1)))
      (body))
    (rule require-row-key-absent-by-lookup-present-v1
      (head
        (RequireRowKeyAbsentByLookupV1
          (FoundRowV1 ?existing) ?table ?key
          (RefusedKeyPresentV1 ?table ?existing)
          (HistoryConsV1
            (CheckedRowPresentV1 ?table ?existing) HistoryNilV1)))
      (body))

    (rule insert-row-v1
      (head
        (InsertRowV1
          ?state ?table ?keyArity ?policy ?row
          ?outcome ?after ?history))
      (body
        (TableInsertV1
          ?state ?table ?keyArity ?policy ?row
          ?tableResult ?tentative ?entry)
        (InsertRowByTableResultV1
          ?tableResult ?state ?table ?row ?tentative ?entry
          ?outcome ?after ?history)))
    (rule insert-row-by-table-result-accepted-v1
      (head
        (InsertRowByTableResultV1
          (InsertedV1 ?table ?row) ?state ?table ?row ?tentative ?entry
          AcceptedV1 ?tentative
          (HistoryConsV1 ?entry HistoryNilV1)))
      (body))
    (rule insert-row-by-table-result-refused-disagrees-v1
      (head
        (InsertRowByTableResultV1
          (RefusedKeyDisagreesV1 ?table ?row ?existing)
          ?state ?table ?row ?tentative ?entry
          (RefusedKeyDisagreesV1 ?table ?row ?existing) ?state
          (HistoryConsV1 ?entry HistoryNilV1)))
      (body))
    (rule insert-row-by-table-result-refused-present-v1
      (head
        (InsertRowByTableResultV1
          (RefusedKeyPresentV1 ?table ?existing)
          ?state ?table ?row ?tentative ?entry
          (RefusedKeyPresentV1 ?table ?existing) ?state
          (HistoryConsV1 ?entry HistoryNilV1)))
      (body))

    (rule require-each-key-absent-nil-v1
      (head
        (RequireEachKeyAbsentV1
          ?state ?occurrence ?table ?keyArity ?rowExpr
          ?values AcceptedV1 HistoryNilV1))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule require-each-key-absent-cons-v1
      (head
        (RequireEachKeyAbsentV1
          ?state ?occurrence ?table ?keyArity ?rowExpr
          ?values
          ?outcome ?history))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (RowExprEvalV1
          ?rowExpr ?occurrence ?value NoSourceContextV1
          (EvaluatedRowV1 ?row))
        (RequireRowKeyAbsentV1
          ?state ?table ?keyArity ?row ?stepOutcome
          (HistoryConsV1 ?entry HistoryNilV1))
        (OutcomeClassV1 ?stepOutcome ?class)
        (RequireEachKeyAbsentAfterStepV1
          ?class ?state ?occurrence ?table ?keyArity ?rowExpr ?rest
          ?stepOutcome ?entry ?outcome ?history)))

    (rule require-each-key-absent-after-step-accepted-v1
      (head
        (RequireEachKeyAbsentAfterStepV1
          OutcomeAcceptedV1 ?state ?occurrence ?table ?keyArity ?rowExpr ?rest
          AcceptedV1 ?entry ?outcome (HistoryConsV1 ?entry ?steps)))
      (body
        (RequireEachKeyAbsentV1
          ?state ?occurrence ?table ?keyArity ?rowExpr
          ?rest ?outcome ?steps)))

    (rule require-each-key-absent-after-step-refused-v1
      (head
        (RequireEachKeyAbsentAfterStepV1
          OutcomeRefusedV1 ?state ?occurrence ?table ?keyArity ?rowExpr ?rest
          ?stepOutcome ?entry ?stepOutcome
          (HistoryConsV1 ?entry HistoryNilV1)))
      (body))

    (rule insert-each-nil-v1
      (head
        (InsertEachV1
          ?state ?occurrence ?table ?keyArity ?policy ?rowExpr
          ?values AcceptedV1 ?state
          (HistoryConsV1
            (CompletedActionV1 ?table) HistoryNilV1)))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule insert-each-cons-v1
      (head
        (InsertEachV1
          ?state ?occurrence ?table ?keyArity ?policy ?rowExpr
          ?values
          ?outcome ?after ?history))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?value ?rest))
        (RowExprEvalV1
          ?rowExpr ?occurrence ?value NoSourceContextV1
          (EvaluatedRowV1 ?row))
        (InsertRowV1
          ?state ?table ?keyArity ?policy ?row
          ?stepOutcome ?next
          (HistoryConsV1 ?entry HistoryNilV1))
        (OutcomeClassV1 ?stepOutcome ?class)
        (InsertEachAfterStepV1
          ?class ?state ?occurrence ?table ?keyArity ?policy ?rowExpr ?rest
          ?stepOutcome ?next ?entry ?outcome ?after ?history)))

    (rule insert-each-after-step-accepted-v1
      (head
        (InsertEachAfterStepV1
          OutcomeAcceptedV1 ?state ?occurrence ?table ?keyArity ?policy
          ?rowExpr ?rest AcceptedV1 ?next ?entry
          ?outcome ?after (HistoryConsV1 ?entry ?steps)))
      (body
        (InsertEachV1
          ?next ?occurrence ?table ?keyArity ?policy ?rowExpr
          ?rest ?outcome ?after ?steps)))

    (rule insert-each-after-step-refused-v1
      (head
        (InsertEachAfterStepV1
          OutcomeRefusedV1 ?state ?occurrence ?table ?keyArity ?policy
          ?rowExpr ?rest ?stepOutcome ?next ?entry
          ?stepOutcome ?state
          (HistoryConsV1 ?entry HistoryNilV1)))
      (body))

    ; ---- filtered occurrence insertion
    (rule insert-each-matching-nil-v1
      (head
        (InsertEachMatchingV1
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?values AcceptedV1 ?state
          (HistoryConsV1
            (CompletedActionV1 ?targetTable) HistoryNilV1)))
      (body
        (ValuesViewV1 ?values ValuesEmptyViewV1)))
    (rule insert-each-matching-cons-v1
      (head
        (InsertEachMatchingV1
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?values ?outcome ?after
          ?history))
      (body
        (ValuesViewV1 ?values (ValuesUnconsViewV1 ?input ?rest))
        (ConditionRowV1
          ?state ?occurrence ?input NoSourceContextV1
          ?conditionTable ?conditionExpr
          ?condition ?conditionEntry)
        (InsertEachMatchingByConditionV1
          ?condition ?conditionEntry
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?input ?rest ?outcome ?after ?history)))
    (rule insert-each-matching-by-condition-missed-v1
      (head
        (InsertEachMatchingByConditionV1
          (ConditionMissedV1 ?conditionRow) ?conditionEntry
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?input ?rest ?outcome ?after
          (HistoryConsV1 ?conditionEntry ?steps)))
      (body
        (InsertEachMatchingV1
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest ?outcome ?after ?steps)))
    (rule insert-each-matching-by-condition-matched-v1
      (head
        (InsertEachMatchingByConditionV1
          (ConditionMatchedV1 ?conditionRow) ?conditionEntry
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?input ?rest ?outcome ?after ?history))
      (body
        (RowExprEvalV1
          ?targetExpr ?occurrence ?input NoSourceContextV1
          (EvaluatedRowV1 ?targetRow))
        (InsertRowV1
          ?state ?targetTable ?keyArity ?policy ?targetRow
          ?stepOutcome ?next
          (HistoryConsV1 ?insertEntry HistoryNilV1))
        (OutcomeClassV1 ?stepOutcome ?class)
        (InsertEachMatchingAfterInsertV1
          ?class ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr ?rest
          ?stepOutcome ?next ?conditionEntry ?insertEntry
          ?outcome ?after ?history)))
    (rule insert-each-matching-after-insert-accepted-v1
      (head
        (InsertEachMatchingAfterInsertV1
          OutcomeAcceptedV1 ?state ?occurrence
          ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest AcceptedV1 ?next ?conditionEntry ?insertEntry
          ?outcome ?after
          (HistoryConsV1
            ?conditionEntry (HistoryConsV1 ?insertEntry ?steps))))
      (body
        (InsertEachMatchingV1
          ?next ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest ?outcome ?after ?steps)))
    (rule insert-each-matching-after-insert-refused-v1
      (head
        (InsertEachMatchingAfterInsertV1
          OutcomeRefusedV1 ?state ?occurrence
          ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest ?stepOutcome ?next ?conditionEntry ?insertEntry
          ?stepOutcome ?state
          (HistoryConsV1
            ?conditionEntry
            (HistoryConsV1 ?insertEntry HistoryNilV1))))
      (body))

    ; ---- ordered source-table copying
    (rule copy-each-row-nil-v1
      (head
        (CopyEachRowV1
          ?state ?occurrence ?targetTable ?keyArity ?policy ?targetExpr
          EntriesNilV1 ?rowIndex AcceptedV1 ?state
          (HistoryConsV1
            (CompletedActionV1 ?targetTable) HistoryNilV1)))
      (body))
    (rule copy-each-row-cons-v1
      (head
        (CopyEachRowV1
          ?state ?occurrence ?targetTable ?keyArity ?policy ?targetExpr
          (EntriesConsV1 (EntryV1 ?seq ?sourceRow ?depth) ?rest)
          ?rowIndex ?outcome ?after ?history))
      (body
        (RowExprEvalV1
          ?targetExpr ?occurrence NoInputV1
          (SourceContextV1 ?sourceRow ?rowIndex NoMatchIndexV1)
          (EvaluatedRowV1 ?targetRow))
        (InsertRowV1
          ?state ?targetTable ?keyArity ?policy ?targetRow
          ?stepOutcome ?next
          (HistoryConsV1 ?insertEntry HistoryNilV1))
        (OutcomeClassV1 ?stepOutcome ?class)
        (CopyEachRowAfterInsertV1
          ?class ?state ?occurrence ?targetTable ?keyArity ?policy
          ?targetExpr ?rest (NatSuccV1 ?rowIndex)
          ?stepOutcome ?next ?insertEntry ?outcome ?after ?history)))
    (rule copy-each-row-after-insert-accepted-v1
      (head
        (CopyEachRowAfterInsertV1
          OutcomeAcceptedV1 ?state ?occurrence ?targetTable
          ?keyArity ?policy ?targetExpr ?rest ?nextRowIndex
          AcceptedV1 ?next ?insertEntry ?outcome ?after
          (HistoryConsV1 ?insertEntry ?steps)))
      (body
        (CopyEachRowV1
          ?next ?occurrence ?targetTable ?keyArity ?policy ?targetExpr
          ?rest ?nextRowIndex ?outcome ?after ?steps)))
    (rule copy-each-row-after-insert-refused-v1
      (head
        (CopyEachRowAfterInsertV1
          OutcomeRefusedV1 ?state ?occurrence ?targetTable
          ?keyArity ?policy ?targetExpr ?rest ?nextRowIndex
          ?stepOutcome ?next ?insertEntry ?stepOutcome ?state
          (HistoryConsV1 ?insertEntry HistoryNilV1)))
      (body))

    ; ---- ordered, filtered source-table copying
    (rule copy-each-row-matching-nil-v1
      (head
        (CopyEachRowMatchingV1
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          EntriesNilV1 ?rowIndex ?matchIndex AcceptedV1 ?state
          (HistoryConsV1
            (CompletedActionV1 ?targetTable) HistoryNilV1)))
      (body))
    (rule copy-each-row-matching-cons-v1
      (head
        (CopyEachRowMatchingV1
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          (EntriesConsV1 (EntryV1 ?seq ?sourceRow ?depth) ?rest)
          ?rowIndex ?matchIndex ?outcome ?after
          ?history))
      (body
        (ConditionRowV1
          ?state ?occurrence NoInputV1
          (SourceContextV1 ?sourceRow ?rowIndex ?matchIndex)
          ?conditionTable ?conditionExpr
          ?condition ?conditionEntry)
        (CopyEachRowMatchingByConditionV1
          ?condition ?conditionEntry
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?sourceRow ?rest ?rowIndex ?matchIndex
          ?outcome ?after ?history)))
    (rule copy-each-row-matching-by-condition-missed-v1
      (head
        (CopyEachRowMatchingByConditionV1
          (ConditionMissedV1 ?conditionRow) ?conditionEntry
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?sourceRow ?rest ?rowIndex ?matchIndex
          ?outcome ?after
          (HistoryConsV1 ?conditionEntry ?steps)))
      (body
        (CopyEachRowMatchingV1
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest (NatSuccV1 ?rowIndex) ?matchIndex
          ?outcome ?after ?steps)))
    (rule copy-each-row-matching-by-condition-matched-v1
      (head
        (CopyEachRowMatchingByConditionV1
          (ConditionMatchedV1 ?conditionRow) ?conditionEntry
          ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?sourceRow ?rest ?rowIndex ?matchIndex
          ?outcome ?after ?history))
      (body
        (RowExprEvalV1
          ?targetExpr ?occurrence NoInputV1
          (SourceContextV1 ?sourceRow ?rowIndex ?matchIndex)
          (EvaluatedRowV1 ?targetRow))
        (InsertRowV1
          ?state ?targetTable ?keyArity ?policy ?targetRow
          ?stepOutcome ?next
          (HistoryConsV1 ?insertEntry HistoryNilV1))
        (OutcomeClassV1 ?stepOutcome ?class)
        (CopyEachRowMatchingAfterInsertV1
          ?class ?state ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr ?rest
          (NatSuccV1 ?rowIndex) (NatSuccV1 ?matchIndex)
          ?stepOutcome ?next ?conditionEntry ?insertEntry
          ?outcome ?after ?history)))
    (rule copy-each-row-matching-after-insert-accepted-v1
      (head
        (CopyEachRowMatchingAfterInsertV1
          OutcomeAcceptedV1 ?state ?occurrence
          ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr ?rest
          ?nextRowIndex ?nextMatchIndex
          AcceptedV1 ?next ?conditionEntry ?insertEntry
          ?outcome ?after
          (HistoryConsV1
            ?conditionEntry (HistoryConsV1 ?insertEntry ?steps))))
      (body
        (CopyEachRowMatchingV1
          ?next ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest ?nextRowIndex ?nextMatchIndex
          ?outcome ?after ?steps)))
    (rule copy-each-row-matching-after-insert-refused-v1
      (head
        (CopyEachRowMatchingAfterInsertV1
          OutcomeRefusedV1 ?state ?occurrence
          ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?rest ?nextRowIndex ?nextMatchIndex
          ?stepOutcome ?next ?conditionEntry ?insertEntry
          ?stepOutcome ?state
          (HistoryConsV1
            ?conditionEntry
            (HistoryConsV1 ?insertEntry HistoryNilV1))))
      (body))

    (rule state-step-require-each-key-absent-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateRequireEachKeyAbsentV1
            ?role ?table ?keyArity ?rowExpr)
          ?outcome ?before ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (RequireEachKeyAbsentV1
          ?before ?occurrence ?table ?keyArity ?rowExpr
          ?values ?outcome ?steps)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireEachKeyAbsentV1
                ?role ?table ?keyArity ?rowExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-insert-each-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateInsertEachV1
            ?role ?table ?keyArity ?policy ?rowExpr)
          ?outcome ?after ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (InsertEachV1
          ?before ?occurrence ?table ?keyArity ?policy ?rowExpr
          ?values ?outcome ?tentative ?steps)
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateInsertEachV1
                ?role ?table ?keyArity ?policy ?rowExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-require-row-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateRequireRowV1 ?table ?rowExpr)
          ?outcome ?before ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrencePositionV1 ?occurrence ?position)
        (RowExprEvalV1
          ?rowExpr ?occurrence NoInputV1 NoSourceContextV1
          (EvaluatedRowV1 ?row))
        (RequireRowV1
          ?before ?table ?row ?outcome ?steps)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireRowV1 ?table ?rowExpr) ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-require-row-key-absent-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateRequireRowKeyAbsentV1
            ?table ?keyArity ?rowExpr)
          ?outcome ?before ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrencePositionV1 ?occurrence ?position)
        (RowExprEvalV1
          ?rowExpr ?occurrence NoInputV1 NoSourceContextV1
          (EvaluatedRowV1 ?row))
        (RequireRowKeyAbsentV1
          ?before ?table ?keyArity ?row ?outcome ?steps)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateRequireRowKeyAbsentV1
                ?table ?keyArity ?rowExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-insert-row-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateInsertRowV1
            ?table ?keyArity ?policy ?rowExpr)
          ?outcome ?after ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrencePositionV1 ?occurrence ?position)
        (RowExprEvalV1
          ?rowExpr ?occurrence NoInputV1 NoSourceContextV1
          (EvaluatedRowV1 ?row))
        (InsertRowV1
          ?before ?table ?keyArity ?policy ?row
          ?outcome ?tentative ?steps)
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateInsertRowV1
                ?table ?keyArity ?policy ?rowExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-insert-each-matching-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateInsertEachMatchingV1
            ?role ?conditionTable ?conditionExpr
            ?targetTable ?keyArity ?policy ?targetExpr)
          ?outcome ?after ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (InsertEachMatchingV1
          ?before ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?values ?outcome ?tentative ?steps)
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateInsertEachMatchingV1
                ?role ?conditionTable ?conditionExpr
                ?targetTable ?keyArity ?policy ?targetExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-copy-each-row-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateCopyEachRowV1
            ?sourceTable ?targetTable ?keyArity ?policy ?targetExpr)
          ?outcome ?after ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (StateTablesOfV1 ?before ?tables)
        (TableOfV1 ?tables ?sourceTable (FoundTableV1 ?sourceEntries))
        (OccurrencePositionV1 ?occurrence ?position)
        (CopyEachRowV1
          ?before ?occurrence ?targetTable ?keyArity ?policy ?targetExpr
          ?sourceEntries NatZeroV1 ?outcome ?tentative ?steps)
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateCopyEachRowV1
                ?sourceTable ?targetTable ?keyArity ?policy ?targetExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule state-step-copy-each-row-matching-v1
      (head
        (StateStepV1
          ?package ?before ?occurrence
          (StateCopyEachRowMatchingV1
            ?sourceTable ?conditionTable ?conditionExpr
            ?targetTable ?keyArity ?policy ?targetExpr)
          ?outcome ?after ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (StateTablesOfV1 ?before ?tables)
        (TableOfV1 ?tables ?sourceTable (FoundTableV1 ?sourceEntries))
        (OccurrencePositionV1 ?occurrence ?position)
        (CopyEachRowMatchingV1
          ?before ?occurrence ?conditionTable ?targetTable
          ?keyArity ?policy ?conditionExpr ?targetExpr
          ?sourceEntries NatZeroV1 NatZeroV1
          ?outcome ?tentative ?steps)
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateCopyEachRowMatchingV1
                ?sourceTable ?conditionTable ?conditionExpr
                ?targetTable ?keyArity ?policy ?targetExpr)
              ?position)
            HistoryNilV1)
          ?steps ?history)))

    ; ---- require the values of a role to be pairwise distinct
    ;
    ; This is the explicit step a guest program uses to require distinct
    ; values, instead of leaving that fact inside an action kind whose name does not
    ; mention it.
    (rule state-step-require-pairwise-distinct-ok-v1
      (head
        (StateStepV1 ?package ?before ?occurrence
                     (StateRequirePairwiseDistinctV1 ?role)
                     AcceptedV1 ?before
                     (HistoryConsV1
                       (EnteredActionV1
                         (StateRequirePairwiseDistinctV1 ?role) ?position)
                       (HistoryConsV1 (CheckedPairwiseDistinctV1 ?values)
                         HistoryNilV1))))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (OccurrenceInterningFaithfulV1 ?occurrence ?role)
        (DistinctScanV1 ?values NatZeroV1 SeenNilV1 DistinctOkV1)))

    (rule state-step-require-pairwise-distinct-duplicate-v1
      (head
        (StateStepV1 ?package ?before ?occurrence
                     (StateRequirePairwiseDistinctV1 ?role)
                     (RefusedDuplicateValueV1 ?value ?first ?second) ?before
                     (HistoryConsV1
                       (EnteredActionV1
                         (StateRequirePairwiseDistinctV1 ?role) ?position)
                       HistoryNilV1)))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (OccurrenceInterningFaithfulV1 ?occurrence ?role)
        (DistinctScanV1 ?values NatZeroV1 SeenNilV1
                        (DuplicateAtV1 ?value ?first ?second))))

    ; ---- insert every pair of a role's values
    ;
    ; Its domain is a distinct list, established by the preceding step.
    (rule state-step-insert-all-pairs-v1
      (head
        (StateStepV1 ?package ?before ?occurrence
                     (StateInsertAllPairsV1 ?role ?table ?policy)
                     ?outcome ?after ?history))
      (body
        (StatePackageOfV1 ?before ?package)
        (OccurrenceRoleNonemptyV1 ?occurrence ?role)
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrencePositionV1 ?occurrence ?position)
        (AllPairsV1 ?values ?pairs)
        (InsertAllPairsV1
          ?before ?table ?policy ?pairs ?outcome ?tentative ?steps)
        (CommitActionStateV1
          ?before ?outcome ?tentative ?after)
        (HistoryAppendV1
          (HistoryConsV1
            (EnteredActionV1
              (StateInsertAllPairsV1 ?role ?table ?policy) ?position)
            HistoryNilV1)
          ?steps ?history)))

    (rule insert-all-pairs-nil-v1
      (head
        (InsertAllPairsV1 ?state ?table ?policy PairsNilV1
                          AcceptedV1 ?state
                          (HistoryConsV1 (CompletedActionV1 ?table) HistoryNilV1)))
      (body))
    (rule insert-all-pairs-step-v1
      (head
        (InsertAllPairsV1 ?state ?table ?policy
                          (PairsConsV1 (PairV1 ?left ?right) ?rest)
                          ?outcome ?after (HistoryConsV1 ?entry ?steps)))
      (body
        (TableInsertV1 ?state ?table (NatSuccV1 (NatSuccV1 NatZeroV1)) ?policy
                       (RowConsV1 ?left (RowConsV1 ?right RowNilV1))
                       (InsertedV1 ?table ?row) ?next ?entry)
        (InsertAllPairsV1 ?next ?table ?policy ?rest ?outcome ?after ?steps)))
    (rule insert-all-pairs-refused-key-v1
      (head
        (InsertAllPairsV1 ?state ?table ?policy
                          (PairsConsV1 (PairV1 ?left ?right) ?rest)
                          (RefusedKeyDisagreesV1 ?table ?row ?existing) ?state
                          (HistoryConsV1 ?entry HistoryNilV1)))
      (body
        (TableInsertV1 ?state ?table (NatSuccV1 (NatSuccV1 NatZeroV1)) ?policy
                       (RowConsV1 ?left (RowConsV1 ?right RowNilV1))
                       (RefusedKeyDisagreesV1 ?table ?row ?existing)
                       ?state ?entry)))

    (rule history-append-nil-v1
      (head (HistoryAppendV1 HistoryNilV1 ?ys ?ys))
      (body))
    (rule history-append-cons-v1
      (head
        (HistoryAppendV1 (HistoryConsV1 ?e ?xs) ?ys (HistoryConsV1 ?e ?out)))
      (body
        (HistoryAppendV1 ?xs ?ys ?out)))

    ; ===================================================== boundary obligation
    ;
    ; Distinctness above is distinctness of INTERNED INDICES.  It means what it
    ; is supposed to mean only if the boundary that interns a role's bytes is
    ; injective on the values of that role.  This states the obligation; the
    ; source fold owes the proof, and a backend that interns differently owes
    ; it too.
    (rule state-interning-injective-v1
      (head (StateInterningInjectiveV1 ?occurrence ?role))
      (body
        (OccurrenceRoleValuesV1 ?occurrence ?role ?values)
        (OccurrenceRoleSymbolsV1 ?occurrence ?role ?symbols)
        (InterningInjectiveOnV1 ?symbols ?values)))

    ; Interning evidence is now LOAD-BEARING: neither branch of the
    ; distinctness step derives an outcome without it.  It was previously
    ; stated in prose beside the rules, which is the same as not stating it --
    ; a reader saw an obligation and the relation did not.
    ;
    ; The two branches need it for DIFFERENT reasons, and saying so is the
    ; point rather than a technicality:
    ;
    ;   ACCEPTING  concludes the SYMBOLS are distinct from the VALUES being
    ;              distinct.  That step needs interning to be a FUNCTION --
    ;              equal symbols intern equal -- whose contrapositive is
    ;              exactly what is used.
    ;   REFUSING   concludes the SYMBOLS repeat from two VALUES being equal.
    ;              That step needs INJECTIVITY.  Without it, two distinct
    ;              variables that happened to intern alike would be refused,
    ;              and a legal database would be called wrong.
    ;
    ; One admission covers both because the boundary establishes both together
    ; -- it is the same table read in two directions.  The refusal direction is
    ; the dangerous one, which is why the evidence is required there too rather
    ; than only where a reviewer first looked.
    (rule occurrence-interning-faithful-v1
      (head (OccurrenceInterningFaithfulV1 ?occurrence ?role))
      (body
        (StateInterningInjectiveV1 ?occurrence ?role)))

    ; ===================================================== ordered composition
    ;
    ; An operation is a SEQUENCE of actions, and the sequence is part of the
    ; meaning.  Without this, "pair insertion never sees a duplicate because
    ; distinctness ran first" is a claim about how a backend happens to walk a
    ; list -- the same mistake as reading failure precedence off premise order.
    ;
    ; Three laws, and each is one rule:
    ;
    ;   accepted  -> the remaining actions run on the RESULTING state
    ;   refused   -> halt; no later action executes, and no later state exists
    ;   history   -> the ordered append of the steps' own intrinsic histories
    ;
    ; The refusal law is what makes step 2's clean domain a consequence rather
    ; than a hope: a repeated-value occurrence refuses at step 1, so step 2 is not reached, so
    ; pair construction is never offered a list with a repeat.
    (rule run-actions-nil-v1
      (head
        (RunActionsV1 ?package ?occurrence ?state ActionsNilV1
                      AcceptedV1 ?state HistoryNilV1))
      (body))

    (rule run-actions-cons-v1
      (head
        (RunActionsV1 ?package ?occurrence ?before
                      (ActionsConsV1 ?action ?rest)
                      ?outcome ?after ?history))
      (body
        (StateStepV1 ?package ?before ?occurrence ?action
                     ?stepOutcome ?middle ?stepHistory)
        (OutcomeClassV1 ?stepOutcome ?class)
        (RunActionsAfterStepV1
          ?class ?package ?occurrence ?before ?middle ?rest
          ?stepOutcome ?stepHistory ?outcome ?after ?history)))

    (rule run-actions-after-step-accepted-v1
      (head
        (RunActionsAfterStepV1
          OutcomeAcceptedV1 ?package ?occurrence ?before ?middle ?rest
          AcceptedV1 ?stepHistory ?outcome ?after ?history))
      (body
        (RunActionsV1 ?package ?occurrence ?middle ?rest
                      ?outcome ?after ?restHistory)
        (HistoryAppendV1 ?stepHistory ?restHistory ?history)))

    ; Halting keeps the state the refused step was offered, not the state that
    ; step would have produced: a refused action contributes no effect.  The
    ; history kept is the refused step's own, so the account ends where the
    ; execution ended.
    (rule run-actions-after-step-refused-v1
      (head
        (RunActionsAfterStepV1
          OutcomeRefusedV1 ?package ?occurrence ?before ?middle ?rest
          ?stepOutcome ?stepHistory ?stepOutcome ?before ?stepHistory))
      (body))

    ; Which outcomes halt.  One rule per refusal constructor, so a refusal
    ; added later without a rule here simply does not halt anything -- which is
    ; a visible gap in a canary, not a silent continuation.
    (rule outcome-class-accepted-v1
      (head (OutcomeClassV1 AcceptedV1 OutcomeAcceptedV1))
      (body))
    (rule outcome-is-refusal-row-absent-v1
      (head (OutcomeIsRefusalV1 (RefusedRowAbsentV1 ?table ?row)))
      (body))
    (rule outcome-class-row-absent-v1
      (head
        (OutcomeClassV1
          (RefusedRowAbsentV1 ?table ?row) OutcomeRefusedV1))
      (body))
    (rule outcome-is-refusal-duplicate-value-v1
      (head (OutcomeIsRefusalV1 (RefusedDuplicateValueV1 ?value ?first ?second)))
      (body))
    (rule outcome-class-duplicate-value-v1
      (head
        (OutcomeClassV1
          (RefusedDuplicateValueV1 ?value ?first ?second)
          OutcomeRefusedV1))
      (body))
    (rule outcome-is-refusal-key-disagrees-v1
      (head (OutcomeIsRefusalV1 (RefusedKeyDisagreesV1 ?table ?row ?existing)))
      (body))
    (rule outcome-class-key-disagrees-v1
      (head
        (OutcomeClassV1
          (RefusedKeyDisagreesV1 ?table ?row ?existing)
          OutcomeRefusedV1))
      (body))
    (rule outcome-is-refusal-key-present-v1
      (head (OutcomeIsRefusalV1 (RefusedKeyPresentV1 ?table ?existing)))
      (body))
    (rule outcome-class-key-present-v1
      (head
        (OutcomeClassV1
          (RefusedKeyPresentV1 ?table ?existing) OutcomeRefusedV1))
      (body))
    (rule outcome-is-refusal-unsupported-row-expr-v1
      (head (OutcomeIsRefusalV1 (RefusedUnsupportedRowExprV1 ?expr)))
      (body))
    (rule outcome-class-unsupported-row-expr-v1
      (head
        (OutcomeClassV1
          (RefusedUnsupportedRowExprV1 ?expr) OutcomeRefusedV1))
      (body))
    (rule outcome-is-refusal-depth-mismatch-v1
      (head
        (OutcomeIsRefusalV1
          (RefusedDepthMismatchV1 ?expected ?actual)))
      (body))
    (rule outcome-class-depth-mismatch-v1
      (head
        (OutcomeClassV1
          (RefusedDepthMismatchV1 ?expected ?actual) OutcomeRefusedV1))
      (body))
    (rule outcome-is-refusal-scope-underflow-v1
      (head
        (OutcomeIsRefusalV1
          (RefusedScopeUnderflowV1 ?actual)))
      (body))
    (rule outcome-class-scope-underflow-v1
      (head
        (OutcomeClassV1
          (RefusedScopeUnderflowV1 ?actual) OutcomeRefusedV1))
      (body))
  ))
