!(import! &self langdef)

(= (mm0:expr:name:set)
   (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 101) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom nil ()))) ()))) ()))))
(= (mm0:expr:name:wff)
   (cons-atom cons (cons-atom (cp 119) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom nil ()))) ()))) ()))))
(= (mm0:expr:name:raw)
   (cons-atom cons (cons-atom (cp 114) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 119) (cons-atom nil ()))) ()))) ()))))
(= (mm0:expr:name:imp)
   (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 109) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom nil ()))) ()))) ()))))
(= (mm0:expr:name:all)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom nil ()))) ()))) ()))))
(= (mm0:expr:name:rawc)
   (cons-atom cons (cons-atom (cp 114) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 119) (cons-atom (cons-atom cons (cons-atom (cp 99) (cons-atom nil ()))) ()))) ()))) ()))))
(= (mm0:expr:name:x) (cons-atom cons (cons-atom (cp 120) (cons-atom nil ()))))
(= (mm0:expr:name:ph) (cons-atom cons (cons-atom (cp 112) (cons-atom (cons-atom cons (cons-atom (cp 104) (cons-atom nil ()))) ()))))

(= (mm0:expr:scalars:imp-ph-ph)
   (MM0MathScalarsConsV1 (cp 105)
     (MM0MathScalarsConsV1 (cp 109)
       (MM0MathScalarsConsV1 (cp 112)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 112)
             (MM0MathScalarsConsV1 (cp 104)
               (MM0MathScalarsConsV1 (cp 32)
                 (MM0MathScalarsConsV1 (cp 112)
                   (MM0MathScalarsConsV1
                     (cp 104) MM0MathScalarsNilV1))))))))))
(= (mm0:expr:scalars:all-x-ph)
   (MM0MathScalarsConsV1 (cp 97)
     (MM0MathScalarsConsV1 (cp 108)
       (MM0MathScalarsConsV1 (cp 108)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 120)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1 (cp 112)
                 (MM0MathScalarsConsV1
                   (cp 104) MM0MathScalarsNilV1)))))))))
(= (mm0:expr:scalars:all-ph-ph)
   (MM0MathScalarsConsV1 (cp 97)
     (MM0MathScalarsConsV1 (cp 108)
       (MM0MathScalarsConsV1 (cp 108)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 112)
             (MM0MathScalarsConsV1 (cp 104)
               (MM0MathScalarsConsV1 (cp 32)
                 (MM0MathScalarsConsV1 (cp 112)
                   (MM0MathScalarsConsV1
                     (cp 104) MM0MathScalarsNilV1))))))))))
(= (mm0:expr:scalars:imp-x-ph)
   (MM0MathScalarsConsV1 (cp 105)
     (MM0MathScalarsConsV1 (cp 109)
       (MM0MathScalarsConsV1 (cp 112)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 120)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1 (cp 112)
                 (MM0MathScalarsConsV1
                   (cp 104) MM0MathScalarsNilV1)))))))))
(= (mm0:expr:scalars:rawc)
   (MM0MathScalarsConsV1 (cp 114)
     (MM0MathScalarsConsV1 (cp 97)
       (MM0MathScalarsConsV1 (cp 119)
         (MM0MathScalarsConsV1
           (cp 99) MM0MathScalarsNilV1)))))

(= (mm0:expr:context)
   (cons-atom cons (cons-atom (MM0BinderV1
       MM0BoundBinderKindV1
       (MM0NamedBinderV1 (mm0:expr:name:x))
       (MM0TypeV1 (mm0:expr:name:set) nil)) (cons-atom (cons-atom cons (cons-atom (MM0BinderV1
         MM0RegularBinderKindV1
         (MM0NamedBinderV1 (mm0:expr:name:ph))
         (MM0TypeV1
           (mm0:expr:name:wff) (cons-atom cons (cons-atom (mm0:expr:name:x) (cons-atom nil ()))))) (cons-atom nil ()))) ()))))

(= (mm0:expr:source-state)
   (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
     (let
       (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
       (langdef:parse-file $mm0-langdef
         "tests/langdef/mm0/expression_environment_positive.mm0")
       (let
         (gslt:result:MM0SourceEnvironmentFromAstV1:1000
           MM0AcceptedV1 $state $history)
         (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)
         (quote $state)))))

; Each canary reports independently: one mismatch cannot hide later evidence.
(= (mm0:expr:check-expression $scalars $expected $accepted $mismatch)
   (let* (($state (mm0:expr:source-state))
          ($context (mm0:expr:context)))
     (if
       (==
         (collapse
           (gslt:mode:MM0RuntimeExpressionElaborateV1:11110
             NatZeroV1 $state $context $scalars))
         ((gslt:result:MM0RuntimeExpressionElaborateV1:11110
            $expected)))
       $accepted
       $mismatch)))

(= (mm0:expr:check-provable $scalars $expected $accepted $mismatch)
   (let* (($state (mm0:expr:source-state))
          ($context (mm0:expr:context)))
     (if
       (==
         (collapse
           (gslt:mode:MM0RuntimeProvableExpressionElaborateV1:11110
             NatZeroV1 $state $context $scalars))
         ((gslt:result:MM0RuntimeProvableExpressionElaborateV1:11110
            $expected)))
       $accepted
       $mismatch)))

!(mm0:expr:check-expression
   (mm0:expr:scalars:imp-ph-ph)
   (MM0ExpressionAcceptedV1
     (MM0ExpressionTermApplicationV1
       (mm0:expr:name:imp)
       (MM0ExpressionArgumentsConsV1
         (MM0ExpressionVariableV1
           (mm0:expr:name:ph) (mm0:expr:name:wff))
         (MM0ExpressionArgumentsConsV1
           (MM0ExpressionVariableV1
             (mm0:expr:name:ph) (mm0:expr:name:wff))
           MM0ExpressionArgumentsNilV1))
       (mm0:expr:name:wff))
     (mm0:expr:name:wff))
   MM0RuntimeExpressionImplicationAcceptedV1
   MM0RuntimeExpressionImplicationMismatchV1)

!(mm0:expr:check-expression
   (mm0:expr:scalars:all-x-ph)
   (MM0ExpressionAcceptedV1
     (MM0ExpressionTermApplicationV1
       (mm0:expr:name:all)
       (MM0ExpressionArgumentsConsV1
         (MM0ExpressionVariableV1
           (mm0:expr:name:x) (mm0:expr:name:set))
         (MM0ExpressionArgumentsConsV1
           (MM0ExpressionVariableV1
             (mm0:expr:name:ph) (mm0:expr:name:wff))
           MM0ExpressionArgumentsNilV1))
       (mm0:expr:name:wff))
     (mm0:expr:name:wff))
   MM0RuntimeExpressionBoundApplicationAcceptedV1
   MM0RuntimeExpressionBoundApplicationMismatchV1)

!(mm0:expr:check-expression
   (mm0:expr:scalars:all-ph-ph)
   (MM0ExpressionRefusedV1
     (MM0RefusedBoundArgumentKindV1
       NatZeroV1 (mm0:expr:name:ph) MM0RegularBinderKindV1))
   MM0RuntimeExpressionBoundArgumentKindRefusedV1
   MM0RuntimeExpressionBoundArgumentKindMismatchV1)

!(mm0:expr:check-expression
   (mm0:expr:scalars:imp-x-ph)
   (MM0ExpressionRefusedV1
     (MM0RefusedExpressionSortV1
       NatZeroV1
       (MM0ExpressionVariableV1
         (mm0:expr:name:x) (mm0:expr:name:set))
       (mm0:expr:name:wff) (mm0:expr:name:set)))
   MM0RuntimeExpressionSortMismatchRefusedV1
   MM0RuntimeExpressionSortMismatchMismatchV1)

!(mm0:expr:check-provable
   (mm0:expr:scalars:imp-ph-ph)
   (MM0ExpressionAcceptedV1
     (MM0ExpressionTermApplicationV1
       (mm0:expr:name:imp)
       (MM0ExpressionArgumentsConsV1
         (MM0ExpressionVariableV1
           (mm0:expr:name:ph) (mm0:expr:name:wff))
         (MM0ExpressionArgumentsConsV1
           (MM0ExpressionVariableV1
             (mm0:expr:name:ph) (mm0:expr:name:wff))
           MM0ExpressionArgumentsNilV1))
       (mm0:expr:name:wff))
     (mm0:expr:name:wff))
   MM0RuntimeProvableExpressionAcceptedV1
   MM0RuntimeProvableExpressionMismatchV1)

!(mm0:expr:check-provable
   (mm0:expr:scalars:rawc)
   (MM0ExpressionRefusedV1
     (MM0RefusedNonProvableSortV1
       NatZeroV1 (mm0:expr:name:raw)))
   MM0RuntimeProvableExpressionNonProvableRefusedV1
   MM0RuntimeProvableExpressionNonProvableMismatchV1)
