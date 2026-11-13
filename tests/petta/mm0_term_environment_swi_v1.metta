!(import! &self ../../langdef/petta/generated/mm0_term_environment_v1)

(= (mm0:term-swi:name:s) (cons-atom cons (cons-atom (cp 115) (cons-atom nil ()))))
(= (mm0:term-swi:name:a) (cons-atom cons (cons-atom (cp 97) (cons-atom nil ()))))
(= (mm0:term-swi:name:set)
   (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 101) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term-swi:name:wff)
   (cons-atom cons (cons-atom (cp 119) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term-swi:name:all)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term-swi:name:x) (cons-atom cons (cons-atom (cp 120) (cons-atom nil ()))))
(= (mm0:term-swi:name:ph) (cons-atom cons (cons-atom (cp 112) (cons-atom (cons-atom cons (cons-atom (cp 104) (cons-atom nil ()))) ()))))
(= (mm0:term-swi:identifier (cons-atom cons (cons-atom $first (cons-atom $rest ()))))
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
   (cons-atom cons (cons-atom (node mm0-type-binder-bound
       (pair
         (cons-atom cons (cons-atom (mm0:term-swi:identifier (mm0:term-swi:name:x)) (cons-atom nil ())))
         (mm0:term-swi:type (mm0:term-swi:name:set) nil))) (cons-atom (cons-atom cons (cons-atom (node mm0-type-binder-regular
         (pair
           (cons-atom cons (cons-atom (mm0:term-swi:identifier (mm0:term-swi:name:ph)) (cons-atom nil ())))
           (mm0:term-swi:type
             (mm0:term-swi:name:wff)
             (cons-atom cons (cons-atom (mm0:term-swi:identifier (mm0:term-swi:name:x)) (cons-atom nil ())))))) (cons-atom nil ()))) ()))))
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
             (cons-atom cons (cons-atom (mm0:term-swi:plain-sort $s) (cons-atom (cons-atom cons (cons-atom (mm0:term-swi:constant $a $s) (cons-atom nil ()))) ()))))))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       MM0AcceptedV1 $environment $history)
     (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast)
     MM0TermEnvironmentSWIAcceptedV1))

!(let* (($set (mm0:term-swi:name:set))
         ($constant
           (cons-atom cons (cons-atom (cp 99) (cons-atom (cons-atom cons (cons-atom (cp 111) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom nil ()))) ()))) ()))) ()))) ()))) ()))) ()))) ()))))
         ($ast
           (node mm0-file
             (cons-atom cons (cons-atom (mm0:term-swi:pure-sort $set) (cons-atom (cons-atom cons (cons-atom (mm0:term-swi:constant $constant $set) (cons-atom nil ()))) ()))))))
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
