(= (mm0:term-direct:name:s) (cons (cp 115) nil))
(= (mm0:term-direct:name:a) (cons (cp 97) nil))
(= (mm0:term-direct:name:set)
   (cons (cp 115) (cons (cp 101) (cons (cp 116) nil))))
(= (mm0:term-direct:name:wff)
   (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))))
(= (mm0:term-direct:name:all)
   (cons (cp 97) (cons (cp 108) (cons (cp 108) nil))))
(= (mm0:term-direct:name:x) (cons (cp 120) nil))
(= (mm0:term-direct:name:ph) (cons (cp 112) (cons (cp 104) nil)))
(= (mm0:term-direct:identifier (cons $first $rest))
   (node mm0-identifier (pair $first $rest)))
(= (mm0:term-direct:plain-sort $name)
   (node mm0-sort-statement
     (pair none
       (pair none
         (pair none
           (pair none (mm0:term-direct:identifier $name)))))))
(= (mm0:term-direct:pure-sort $name)
   (node mm0-sort-statement
     (pair
       (some (cons (cp 112)
               (cons (cp 117) (cons (cp 114) (cons (cp 101) nil)))))
       (pair none
         (pair none
           (pair none (mm0:term-direct:identifier $name)))))))
(= (mm0:term-direct:constant $term $sort)
   (node mm0-term-statement
     (pair (mm0:term-direct:identifier $term)
       (pair nil
         (node mm0-arrow-type
           (pair
             (node mm0-type
               (pair (mm0:term-direct:identifier $sort) nil))
             none))))))
(= (mm0:term-direct:type $sort $dependencies)
   (node mm0-type
     (pair (mm0:term-direct:identifier $sort) $dependencies)))
(= (mm0:term-direct:dependent-binders)
   (cons
     (node mm0-type-binder-bound
       (pair
         (cons
           (mm0:term-direct:identifier (mm0:term-direct:name:x)) nil)
         (mm0:term-direct:type (mm0:term-direct:name:set) nil)))
     (cons
       (node mm0-type-binder-regular
         (pair
           (cons
             (mm0:term-direct:identifier (mm0:term-direct:name:ph)) nil)
           (mm0:term-direct:type
             (mm0:term-direct:name:wff)
             (cons
               (mm0:term-direct:identifier (mm0:term-direct:name:x)) nil))))
       nil)))
(= (mm0:term-direct:wff-return)
   (node mm0-arrow-type
     (pair (mm0:term-direct:type (mm0:term-direct:name:wff) nil) none)))
(= (mm0:term-direct:dependent-sorts)
   (MM0SortEntriesConsV1
     (MM0SortEntryV1
       (mm0:term-direct:name:set)
       (MM0SortModifiersV1
         MM0ModifierAbsentV1 MM0ModifierAbsentV1
         MM0ModifierAbsentV1 MM0ModifierAbsentV1)
       NatZeroV1)
     (MM0SortEntriesConsV1
       (MM0SortEntryV1
         (mm0:term-direct:name:wff)
         (MM0SortModifiersV1
           MM0ModifierAbsentV1 MM0ModifierPresentV1
           MM0ModifierPresentV1 MM0ModifierAbsentV1)
         (NatSuccV1 NatZeroV1))
       MM0SortEntriesNilV1)))

(= (mm0:term-direct:positive-ast)
   (node mm0-file
     (cons (mm0:term-direct:plain-sort (mm0:term-direct:name:s))
       (cons
         (mm0:term-direct:constant
           (mm0:term-direct:name:a) (mm0:term-direct:name:s))
         nil))))
(= (mm0:term-direct:pure-target-ast)
   (node mm0-file
     (cons (mm0:term-direct:pure-sort (mm0:term-direct:name:s))
       (cons
         (mm0:term-direct:constant
           (mm0:term-direct:name:a) (mm0:term-direct:name:s))
         nil))))

!(let* (($ast (mm0:term-direct:positive-ast))
         ($s (mm0:term-direct:name:s))
         ($a (mm0:term-direct:name:a)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       MM0AcceptedV1
       (MM0TextualEnvironmentV1
         $sorts
         (MM0DeclarationEntriesConsV1
           (MM0TermEntryV1
             $a
             (MM0TermSignatureV1 nil (MM0TypeV1 $s nil))
             (NatSuccV1 NatZeroV1)
             MM0RuntimeTextualSourceV1)
           MM0DeclarationEntriesNilV1))
       $history)
     (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)
     MM0TermEnvironmentDirectAcceptedV1))

!(let* (($ast (mm0:term-direct:pure-target-ast))
         ($s (mm0:term-direct:name:s))
         ($a (mm0:term-direct:name:a)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedPureTermTargetV1
         (NatSuccV1 NatZeroV1) $a $s)
       $environment $history)
     (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)
     MM0TermEnvironmentDirectPureTargetRefusedV1))

!(let* (($name (mm0:term-direct:name:all))
         ($set (mm0:term-direct:name:set))
         ($wff (mm0:term-direct:name:wff))
         ($x (mm0:term-direct:name:x))
         ($ph (mm0:term-direct:name:ph))
         ($sorts (mm0:term-direct:dependent-sorts))
         ($binders (mm0:term-direct:dependent-binders))
         ($arrow (mm0:term-direct:wff-return)))
   (let $actual
     (gslt:fn:MM0ValidateTermSignatureV1:111110
       NatZeroV1 $name $sorts $binders $arrow)
     (if
       (== $actual
         (gslt:result:MM0ValidateTermSignatureV1:111110
           (MM0CheckAcceptedV1
             (MM0TermSignatureV1
               (cons
                 (MM0BinderV1
                   MM0BoundBinderKindV1 (MM0NamedBinderV1 $x)
                   (MM0TypeV1 $set nil))
                 (cons
                   (MM0BinderV1
                     MM0RegularBinderKindV1 (MM0NamedBinderV1 $ph)
                     (MM0TypeV1 $wff (cons $x nil)))
                   nil))
               (MM0TypeV1 $wff nil)))))
       MM0TermEnvironmentDependentSignatureAcceptedV1
       MM0TermEnvironmentDependentSignatureMismatchV1)))
