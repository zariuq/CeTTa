!(import! &self ../../langdef/petta/generated/mm0_term_environment_v1)

(= (mm0:term-swi:name:s) (cons (cp 115) nil))
(= (mm0:term-swi:name:a) (cons (cp 97) nil))
(= (mm0:term-swi:name:set)
   (cons (cp 115) (cons (cp 101) (cons (cp 116) nil))))
(= (mm0:term-swi:name:wff)
   (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))))
(= (mm0:term-swi:name:all)
   (cons (cp 97) (cons (cp 108) (cons (cp 108) nil))))
(= (mm0:term-swi:name:x) (cons (cp 120) nil))
(= (mm0:term-swi:name:ph) (cons (cp 112) (cons (cp 104) nil)))
(= (mm0:term-swi:identifier (cons $first $rest))
   (node mm0-identifier (pair $first $rest)))
(= (mm0:term-swi:plain-sort $name)
   (node mm0-sort-statement
     (pair none
       (pair none
         (pair none
           (pair none (mm0:term-swi:identifier $name)))))))
(= (mm0:term-swi:pure-sort $name)
   (node mm0-sort-statement
     (pair (some pure-source-v1)
       (pair none
         (pair none
           (pair none (mm0:term-swi:identifier $name)))))))
(= (mm0:term-swi:constant $term $sort)
   (node mm0-term-statement
     (pair (mm0:term-swi:identifier $term)
       (pair nil
         (node mm0-arrow-type
           (pair
             (node mm0-type
               (pair (mm0:term-swi:identifier $sort) nil))
             none))))))
(= (mm0:term-swi:type $sort $dependencies)
   (node mm0-type
     (pair (mm0:term-swi:identifier $sort) $dependencies)))
(= (mm0:term-swi:dependent-binders)
   (cons
     (node mm0-type-binder-bound
       (pair
         (cons
           (mm0:term-swi:identifier (mm0:term-swi:name:x)) nil)
         (mm0:term-swi:type (mm0:term-swi:name:set) nil)))
     (cons
       (node mm0-type-binder-regular
         (pair
           (cons
             (mm0:term-swi:identifier (mm0:term-swi:name:ph)) nil)
           (mm0:term-swi:type
             (mm0:term-swi:name:wff)
             (cons
               (mm0:term-swi:identifier (mm0:term-swi:name:x)) nil))))
       nil)))
(= (mm0:term-swi:wff-return)
   (node mm0-arrow-type
     (pair (mm0:term-swi:type (mm0:term-swi:name:wff) nil) none)))
(= (mm0:term-swi:dependent-sorts)
   (MM0SortEntriesConsV1
     (MM0SortEntryV1
       (mm0:term-swi:name:set)
       (MM0SortModifiersV1
         MM0ModifierAbsentV1 MM0ModifierAbsentV1
         MM0ModifierAbsentV1 MM0ModifierAbsentV1)
       NatZeroV1)
     (MM0SortEntriesConsV1
       (MM0SortEntryV1
         (mm0:term-swi:name:wff)
         (MM0SortModifiersV1
           MM0ModifierAbsentV1 MM0ModifierPresentV1
           MM0ModifierPresentV1 MM0ModifierAbsentV1)
         (NatSuccV1 NatZeroV1))
       MM0SortEntriesNilV1)))

!(let* (($s (mm0:term-swi:name:s))
         ($a (mm0:term-swi:name:a))
         ($ast
           (node mm0-file
             (cons (mm0:term-swi:plain-sort $s)
               (cons (mm0:term-swi:constant $a $s) nil)))))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       MM0AcceptedV1 $environment $history)
     (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast)
     MM0TermEnvironmentSWIAcceptedV1))

!(let* (($set (mm0:term-swi:name:set))
         ($constant
           (cons (cp 99)
             (cons (cp 111)
               (cons (cp 110)
                 (cons (cp 115)
                   (cons (cp 116)
                     (cons (cp 97)
                       (cons (cp 110) (cons (cp 116) nil)))))))))
         ($ast
           (node mm0-file
             (cons (mm0:term-swi:pure-sort $set)
               (cons (mm0:term-swi:constant $constant $set) nil)))))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedPureTermTargetV1
         (NatSuccV1 NatZeroV1) $constant $set)
       $environment $history)
     (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast)
     MM0TermEnvironmentSWIPureTargetRefusedV1))

!(let* (($name (mm0:term-swi:name:all))
         ($sorts (mm0:term-swi:dependent-sorts))
         ($binders (mm0:term-swi:dependent-binders))
         ($arrow (mm0:term-swi:wff-return)))
   (let
     (gslt:result:MM0ValidateTermSignatureV1:111110
       (MM0CheckAcceptedV1 $signature))
     (gslt:mode:MM0ValidateTermSignatureV1:111110
       NatZeroV1 $name $sorts $binders $arrow)
     MM0TermEnvironmentSWIDependentSignatureAcceptedV1))
