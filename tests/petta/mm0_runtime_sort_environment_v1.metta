!(import! &self langdef)

(= (mm0:test:runtime-wff-name)
   (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))))
(= (mm0:test:runtime-set-name)
   (cons (cp 115) (cons (cp 101) (cons (cp 116) nil))))
(= (mm0:test:runtime-provable-modifiers)
   (MM0SortModifiersV1
     MM0ModifierAbsentV1 MM0ModifierAbsentV1
     MM0ModifierPresentV1 MM0ModifierAbsentV1))
(= (mm0:test:runtime-strict-modifiers)
   (MM0SortModifiersV1
     MM0ModifierAbsentV1 MM0ModifierPresentV1
     MM0ModifierAbsentV1 MM0ModifierAbsentV1))

; The .mm0 path appears only here, at runtime.  The generated program contains
; the reusable sort semantics and no database-specific statements.
!(let* (($wff (mm0:test:runtime-wff-name))
         ($set (mm0:test:runtime-set-name))
         ($provable (mm0:test:runtime-provable-modifiers))
         ($strict (mm0:test:runtime-strict-modifiers))
         ($mm0-langdef (langdef:load "langdef/mm0/langdef.metta")))
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/sort_repeated_identical.mm0")
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
         MM0RuntimeSortEnvironmentAcceptedV1
         MM0RuntimeSortEnvironmentMismatchV1))))

!(let* (($wff (mm0:test:runtime-wff-name))
         ($provable (mm0:test:runtime-provable-modifiers))
         ($strict (mm0:test:runtime-strict-modifiers))
         ($mm0-langdef (langdef:load "langdef/mm0/langdef.metta")))
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/sort_redeclared_conflict.mm0")
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
         MM0RuntimeSortRedeclarationRefusedV1
         MM0RuntimeSortRedeclarationMismatchV1))))
