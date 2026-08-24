(= (mm0:test:wff-name)
   (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))))
(= (mm0:test:set-name)
   (cons (cp 115) (cons (cp 101) (cons (cp 116) nil))))
(= (mm0:test:provable-modifiers)
   (MM0SortModifiersV1
     MM0ModifierAbsentV1 MM0ModifierAbsentV1
     MM0ModifierPresentV1 MM0ModifierAbsentV1))
(= (mm0:test:strict-modifiers)
   (MM0SortModifiersV1
     MM0ModifierAbsentV1 MM0ModifierPresentV1
     MM0ModifierAbsentV1 MM0ModifierAbsentV1))
(= (mm0:test:identifier (cons $first $rest))
   (node mm0-identifier
     (pair $first $rest)))

; These two constructors mirror parser output without embedding any source
; file in the generated verifier.
(= (mm0:test:provable-sort $name)
   (node mm0-sort-statement
     (pair none
       (pair none
         (pair (some (cons (cp 112)
                       (cons (cp 114)
                         (cons (cp 111)
                           (cons (cp 118)
                             (cons (cp 97)
                               (cons (cp 98)
                                 (cons (cp 108)
                                   (cons (cp 101) nil)))))))))
           (pair none (mm0:test:identifier $name)))))))
(= (mm0:test:strict-sort $name)
   (node mm0-sort-statement
     (pair none
       (pair (some (cons (cp 115)
                     (cons (cp 116)
                       (cons (cp 114)
                         (cons (cp 105)
                           (cons (cp 99)
                             (cons (cp 116) nil)))))))
         (pair none (pair none (mm0:test:identifier $name)))))))

(= (mm0:test:positive-ast)
   (node mm0-file
     (cons (mm0:test:provable-sort (mm0:test:wff-name))
       (cons (mm0:test:provable-sort (mm0:test:wff-name))
         (cons (mm0:test:strict-sort (mm0:test:set-name)) nil)))))
(= (mm0:test:conflict-ast)
   (node mm0-file
     (cons (mm0:test:provable-sort (mm0:test:wff-name))
       (cons (mm0:test:strict-sort (mm0:test:wff-name)) nil))))

!(let* (($ast (eval (quote (mm0:test:positive-ast))))
         ($wff (mm0:test:wff-name))
         ($set (mm0:test:set-name))
         ($provable (mm0:test:provable-modifiers))
         ($strict (mm0:test:strict-modifiers)))
   (let $actual
     (eval
       (quote
         (gslt:fn:MM0SortEnvironmentFromAstV1:1000 $ast)))
     (if
       (== $actual
         (gslt:result:MM0SortEnvironmentFromAstV1:1000
           MM0AcceptedV1
           (MM0SortEntriesConsV1
             (MM0SortEntryV1 $wff $provable NatZeroV1)
             (MM0SortEntriesConsV1
               (MM0SortEntryV1
                 $set $strict (NatSuccV1 (NatSuccV1 NatZeroV1)))
               MM0SortEntriesNilV1))
           (MM0SortHistoryConsV1
             (MM0DeclaredSortV1 NatZeroV1 $wff $provable)
             (MM0SortHistoryConsV1
               (MM0ReusedSortV1
                 (NatSuccV1 NatZeroV1) $wff NatZeroV1)
               (MM0SortHistoryConsV1
                 (MM0DeclaredSortV1
                   (NatSuccV1 (NatSuccV1 NatZeroV1)) $set $strict)
                 MM0SortHistoryNilV1)))))
       MM0SortEnvironmentPositiveAcceptedV1
       MM0SortEnvironmentPositiveMismatchV1)))

!(let* (($ast (eval (quote (mm0:test:conflict-ast))))
         ($wff (mm0:test:wff-name))
         ($provable (mm0:test:provable-modifiers))
         ($strict (mm0:test:strict-modifiers)))
   (let $actual
     (eval
       (quote
         (gslt:fn:MM0SortEnvironmentFromAstV1:1000 $ast)))
     (if
       (== $actual
         (gslt:result:MM0SortEnvironmentFromAstV1:1000
           (MM0RefusedSortRedeclarationV1
             (NatSuccV1 NatZeroV1) $wff $provable $strict)
           (MM0SortEntriesConsV1
             (MM0SortEntryV1 $wff $provable NatZeroV1)
             MM0SortEntriesNilV1)
           (MM0SortHistoryConsV1
             (MM0DeclaredSortV1 NatZeroV1 $wff $provable)
             (MM0SortHistoryConsV1
               (MM0RefusedSortRedeclarationV1
                 (NatSuccV1 NatZeroV1) $wff $provable $strict)
               MM0SortHistoryNilV1))))
       MM0SortEnvironmentConflictRefusedV1
       MM0SortEnvironmentConflictMismatchV1)))

; A conflicting redeclaration must not also have an accepted execution.
!(let $ast (eval (quote (mm0:test:conflict-ast)))
   (collapse
     (let
       (gslt:result:MM0SortEnvironmentFromAstV1:1000
         MM0AcceptedV1 $environment $history)
       (eval
         (quote
           (gslt:fn:MM0SortEnvironmentFromAstV1:1000 $ast)))
       MM0SortEnvironmentConflictAcceptedV1)))
