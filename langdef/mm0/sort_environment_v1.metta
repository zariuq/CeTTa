; Ordered semantics of MM0 sort declarations.
;
; Concrete .mm0 files are runtime parser inputs.  This presentation consumes
; the parser's semantic AST and defines the resulting sort environment,
; outcome, and history.  It is intentionally a sort-declaration component,
; not a claim to implement the remaining MM0 declaration forms.

(gslt-presentation-v1 MM0SortEnvironmentV1
  (signature
    (operator node 2)
    (operator pair 2)
    (operator some 1)
    (operator cons 2)
    (operator different 2)
    (operator NatSuccV1 1)
    (operator MM0OptionPresenceV1 2)
    (operator MM0IdentifierScalarsV1 2)
    (operator MM0SortStatementV1 3)
    (operator MM0SortModifiersV1 4)
    (operator MM0SortEntryV1 3)
    (operator MM0SortEntriesConsV1 2)
    (operator MM0SortLookupV1 3)
    (operator MM0SortFoundV1 2)
    (operator MM0SortEntriesAppendV1 3)
    (operator MM0SortApplyV1 8)
    (operator MM0SortStepV1 6)
    (operator MM0SortFoldV1 6)
    (operator MM0SortFoldAfterStepV1 8)
    (operator MM0SortEnvironmentFromAstV1 4)
    (operator MM0SortHistoryConsV1 2)
    (operator MM0DeclaredSortV1 3)
    (operator MM0ReusedSortV1 3)
    (operator MM0RefusedSortRedeclarationV1 4))
  (equations)
  (rewrites
    ; The parser has already admitted the keyword at each option position.  A
    ; semantic modifier therefore observes only presence, never keyword text.
    (rule mm0-option-presence-absent-v1
      (head
        (MM0OptionPresenceV1 none MM0ModifierAbsentV1))
      (body))
    (rule mm0-option-presence-present-v1
      (head
        (MM0OptionPresenceV1
          (some ?source) MM0ModifierPresentV1))
      (body))

    (rule mm0-identifier-scalars-v1
      (head
        (MM0IdentifierScalarsV1
          (node mm0-identifier (pair ?first ?rest))
          (cons ?first ?rest)))
      (body))

    ; The four modifier positions are fixed by the MM0 grammar:
    ; pure, strict, provable, free.
    (rule mm0-sort-statement-v1
      (head
        (MM0SortStatementV1
          (node mm0-sort-statement
            (pair ?pure
              (pair ?strict
                (pair ?provable
                  (pair ?free ?identifier)))))
          ?name
          (MM0SortModifiersV1
            ?pureFlag ?strictFlag ?provableFlag ?freeFlag)))
      (body
        (MM0OptionPresenceV1 ?pure ?pureFlag)
        (MM0OptionPresenceV1 ?strict ?strictFlag)
        (MM0OptionPresenceV1 ?provable ?provableFlag)
        (MM0OptionPresenceV1 ?free ?freeFlag)
        (MM0IdentifierScalarsV1 ?identifier ?name)))

    ; Lookup is total on a finite environment: it returns Found or Missing.
    (rule mm0-sort-lookup-missing-v1
      (head
        (MM0SortLookupV1
          ?name MM0SortEntriesNilV1 MM0SortMissingV1))
      (body))
    (rule mm0-sort-lookup-found-v1
      (head
        (MM0SortLookupV1
          ?name
          (MM0SortEntriesConsV1
            (MM0SortEntryV1 ?name ?modifiers ?firstPosition)
            ?rest)
          (MM0SortFoundV1 ?modifiers ?firstPosition)))
      (body))
    (rule mm0-sort-lookup-tail-v1
      (head
        (MM0SortLookupV1
          ?name
          (MM0SortEntriesConsV1
            (MM0SortEntryV1 ?other ?modifiers ?firstPosition)
            ?rest)
          ?result))
      (body
        (different ?name ?other)
        (MM0SortLookupV1 ?name ?rest ?result)))

    ; Append retains declaration order in the didactic environment.
    (rule mm0-sort-entries-append-nil-v1
      (head
        (MM0SortEntriesAppendV1
          MM0SortEntriesNilV1 ?entry
          (MM0SortEntriesConsV1 ?entry MM0SortEntriesNilV1)))
      (body))
    (rule mm0-sort-entries-append-cons-v1
      (head
        (MM0SortEntriesAppendV1
          (MM0SortEntriesConsV1 ?head ?rest) ?entry
          (MM0SortEntriesConsV1 ?head ?after)))
      (body
        (MM0SortEntriesAppendV1 ?rest ?entry ?after)))

    ; A fresh declaration extends the environment.  An exact redeclaration is
    ; accepted and reuses the first declaration.  Changing its modifiers is a
    ; semantic refusal and leaves the prior environment intact.
    (rule mm0-sort-apply-missing-v1
      (head
        (MM0SortApplyV1
          ?position ?name ?modifiers MM0SortMissingV1 ?before
          MM0AcceptedV1 ?after
          (MM0DeclaredSortV1 ?position ?name ?modifiers)))
      (body
        (MM0SortEntriesAppendV1
          ?before
          (MM0SortEntryV1 ?name ?modifiers ?position)
          ?after)))
    (rule mm0-sort-apply-existing-equal-v1
      (head
        (MM0SortApplyV1
          ?position ?name ?modifiers
          (MM0SortFoundV1 ?modifiers ?firstPosition) ?state
          MM0AcceptedV1 ?state
          (MM0ReusedSortV1 ?position ?name ?firstPosition)))
      (body))
    (rule mm0-sort-apply-existing-different-v1
      (head
        (MM0SortApplyV1
          ?position ?name ?newModifiers
          (MM0SortFoundV1 ?oldModifiers ?firstPosition) ?state
          (MM0RefusedSortRedeclarationV1
            ?position ?name ?oldModifiers ?newModifiers)
          ?state
          (MM0RefusedSortRedeclarationV1
            ?position ?name ?oldModifiers ?newModifiers)))
      (body
        (different ?newModifiers ?oldModifiers)))

    (rule mm0-sort-step-v1
      (head
        (MM0SortStepV1
          ?position ?statement ?before ?outcome ?after ?historyEntry))
      (body
        (MM0SortStatementV1 ?statement ?name ?modifiers)
        (MM0SortLookupV1 ?name ?before ?lookup)
        (MM0SortApplyV1
          ?position ?name ?modifiers ?lookup ?before
          ?outcome ?after ?historyEntry)))

    (rule mm0-sort-fold-nil-v1
      (head
        (MM0SortFoldV1
          nil ?position ?state
          MM0AcceptedV1 ?state MM0SortHistoryNilV1))
      (body))
    (rule mm0-sort-fold-cons-v1
      (head
        (MM0SortFoldV1
          (cons ?statement ?rest) ?position ?before
          ?outcome ?after ?history))
      (body
        (MM0SortStepV1
          ?position ?statement ?before
          ?stepOutcome ?next ?historyEntry)
        (MM0SortFoldAfterStepV1
          ?rest (NatSuccV1 ?position) ?stepOutcome ?next ?historyEntry
          ?outcome ?after ?history)))
    (rule mm0-sort-fold-after-accepted-v1
      (head
        (MM0SortFoldAfterStepV1
          ?rest ?nextPosition MM0AcceptedV1 ?next ?historyEntry
          ?outcome ?after
          (MM0SortHistoryConsV1 ?historyEntry ?history)))
      (body
        (MM0SortFoldV1
          ?rest ?nextPosition ?next ?outcome ?after ?history)))
    (rule mm0-sort-fold-after-refused-v1
      (head
        (MM0SortFoldAfterStepV1
          ?rest ?nextPosition
          (MM0RefusedSortRedeclarationV1
            ?position ?name ?oldModifiers ?newModifiers)
          ?state ?historyEntry
          (MM0RefusedSortRedeclarationV1
            ?position ?name ?oldModifiers ?newModifiers)
          ?state
          (MM0SortHistoryConsV1
            ?historyEntry MM0SortHistoryNilV1)))
      (body))

    ; This entry intentionally accepts only files whose statements are sort
    ; declarations.  Other declaration components will compose at the ordered
    ; file-fold boundary instead of being silently skipped here.
    (rule mm0-sort-environment-from-ast-v1
      (head
        (MM0SortEnvironmentFromAstV1
          (node mm0-file ?statements) ?outcome ?environment ?history))
      (body
        (MM0SortFoldV1
          ?statements NatZeroV1 MM0SortEntriesNilV1
          ?outcome ?environment ?history)))))
