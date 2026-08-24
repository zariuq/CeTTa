!(gslt-rho:query:MM0SortEnvironmentFromAstV1
   QueryMM0SortEnvironmentEmptyV1
   (MM0SortEnvironmentFromAstV1
     (node mm0-file nil)
     MM0AcceptedV1
     MM0SortEntriesNilV1
     MM0SortHistoryNilV1))

!(gslt-rho:query:MM0SortEnvironmentFromAstV1
   QueryMM0SortEnvironmentFreshV1
   (MM0SortEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair (some provable-source-v1)
                 (pair none
                   (node mm0-identifier
                     (pair (cp 119)
                       (cons (cp 102) (cons (cp 102) nil)))))))))
         nil))
     MM0AcceptedV1
     (MM0SortEntriesConsV1
       (MM0SortEntryV1
         (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
         (MM0SortModifiersV1
           MM0ModifierAbsentV1 MM0ModifierAbsentV1
           MM0ModifierPresentV1 MM0ModifierAbsentV1)
         NatZeroV1)
       MM0SortEntriesNilV1)
     (MM0SortHistoryConsV1
       (MM0DeclaredSortV1
         NatZeroV1
         (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
         (MM0SortModifiersV1
           MM0ModifierAbsentV1 MM0ModifierAbsentV1
           MM0ModifierPresentV1 MM0ModifierAbsentV1))
       MM0SortHistoryNilV1)))

!(gslt-rho:query:MM0SortEnvironmentFromAstV1
   QueryMM0SortEnvironmentConflictV1
   (MM0SortEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair (some provable-source-v1)
                 (pair none
                   (node mm0-identifier
                     (pair (cp 119)
                       (cons (cp 102) (cons (cp 102) nil)))))))))
         (cons
           (node mm0-sort-statement
             (pair none
               (pair (some strict-source-v1)
                 (pair none
                   (pair none
                     (node mm0-identifier
                       (pair (cp 119)
                         (cons (cp 102) (cons (cp 102) nil)))))))))
           nil)))
     (MM0RefusedSortRedeclarationV1
       (NatSuccV1 NatZeroV1)
       (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
       (MM0SortModifiersV1
         MM0ModifierAbsentV1 MM0ModifierAbsentV1
         MM0ModifierPresentV1 MM0ModifierAbsentV1)
       (MM0SortModifiersV1
         MM0ModifierAbsentV1 MM0ModifierPresentV1
         MM0ModifierAbsentV1 MM0ModifierAbsentV1))
     (MM0SortEntriesConsV1
       (MM0SortEntryV1
         (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
         (MM0SortModifiersV1
           MM0ModifierAbsentV1 MM0ModifierAbsentV1
           MM0ModifierPresentV1 MM0ModifierAbsentV1)
         NatZeroV1)
       MM0SortEntriesNilV1)
     (MM0SortHistoryConsV1
       (MM0DeclaredSortV1
         NatZeroV1
         (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
         (MM0SortModifiersV1
           MM0ModifierAbsentV1 MM0ModifierAbsentV1
           MM0ModifierPresentV1 MM0ModifierAbsentV1))
       (MM0SortHistoryConsV1
         (MM0RefusedSortRedeclarationV1
           (NatSuccV1 NatZeroV1)
           (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
           (MM0SortModifiersV1
             MM0ModifierAbsentV1 MM0ModifierAbsentV1
             MM0ModifierPresentV1 MM0ModifierAbsentV1)
           (MM0SortModifiersV1
             MM0ModifierAbsentV1 MM0ModifierPresentV1
             MM0ModifierAbsentV1 MM0ModifierAbsentV1))
         MM0SortHistoryNilV1))))

; The same conflicting source cannot also yield an accepted outcome.
!(gslt-rho:query:MM0SortEnvironmentFromAstV1
   QueryMM0SortEnvironmentConflictAcceptedV1
   (MM0SortEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair (some provable-source-v1)
                 (pair none
                   (node mm0-identifier
                     (pair (cp 119)
                       (cons (cp 102) (cons (cp 102) nil)))))))))
         (cons
           (node mm0-sort-statement
             (pair none
               (pair (some strict-source-v1)
                 (pair none
                   (pair none
                     (node mm0-identifier
                       (pair (cp 119)
                         (cons (cp 102) (cons (cp 102) nil)))))))))
           nil)))
     MM0AcceptedV1 $environment $history))
