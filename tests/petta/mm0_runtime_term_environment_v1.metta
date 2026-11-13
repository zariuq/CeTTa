!(import! &self langdef)

(= (mm0:term:name:set)
   (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 101) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term:name:wff)
   (cons-atom cons (cons-atom (cp 119) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term:name:all)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term:name:imp)
   (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 109) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term:name:x) (cons-atom cons (cons-atom (cp 120) (cons-atom nil ()))))
(= (mm0:term:name:ph) (cons-atom cons (cons-atom (cp 112) (cons-atom (cons-atom cons (cons-atom (cp 104) (cons-atom nil ()))) ()))))
(= (mm0:term:name:constant)
   (cons-atom cons (cons-atom (cp 99) (cons-atom (cons-atom cons (cons-atom (cp 111) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom nil ()))) ()))) ()))) ()))) ()))) ()))) ()))) ()))))
(= (mm0:term:name:atom)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 116) (cons-atom (cons-atom cons (cons-atom (cp 111) (cons-atom (cons-atom cons (cons-atom (cp 109) (cons-atom nil ()))) ()))) ()))) ()))))
(= (mm0:term:name:bind)
   (cons-atom cons (cons-atom (cp 98) (cons-atom (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 100) (cons-atom nil ()))) ()))) ()))) ()))))
(= (mm0:term:name:missing)
   (cons-atom cons (cons-atom (cp 109) (cons-atom (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 103) (cons-atom nil ()))) ()))) ()))) ()))) ()))) ()))) ()))))
(= (mm0:term:name:bad)
   (cons-atom cons (cons-atom (cp 98) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 100) (cons-atom nil ()))) ()))) ()))))
(= (mm0:term:name:value)
   (cons-atom cons (cons-atom (cp 118) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 108) (cons-atom (cons-atom cons (cons-atom (cp 117) (cons-atom (cons-atom cons (cons-atom (cp 101) (cons-atom nil ()))) ()))) ()))) ()))) ()))))

; Positive: explicit bound/dependent binders and anonymous arrow arguments
; normalize into one ordered term-signature representation.
!(let* (($set (mm0:term:name:set))
         ($wff (mm0:term:name:wff))
         ($all (mm0:term:name:all))
         ($imp (mm0:term:name:imp))
         ($x (mm0:term:name:x))
         ($ph (mm0:term:name:ph)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       MM0AcceptedV1
       (MM0TextualEnvironmentV1
         $sorts
         (MM0DeclarationEntriesConsV1
           (MM0TermEntryV1
             $all
             (MM0TermSignatureV1
               (cons-atom cons (cons-atom (MM0BinderV1
                   MM0BoundBinderKindV1 (MM0NamedBinderV1 $x)
                   (MM0TypeV1 $set nil)) (cons-atom (cons-atom cons (cons-atom (MM0BinderV1
                     MM0RegularBinderKindV1 (MM0NamedBinderV1 $ph)
                     (MM0TypeV1 $wff (cons-atom cons (cons-atom $x (cons-atom nil ()))))) (cons-atom nil ()))) ())))
               (MM0TypeV1 $wff nil))
             (NatSuccV1 (NatSuccV1 NatZeroV1))
             MM0RuntimeTextualSourceV1)
           (MM0DeclarationEntriesConsV1
             (MM0TermEntryV1
               $imp
               (MM0TermSignatureV1
                 (cons-atom cons (cons-atom (MM0BinderV1
                     MM0RegularBinderKindV1 MM0AnonymousBinderV1
                     (MM0TypeV1 $wff nil)) (cons-atom (cons-atom cons (cons-atom (MM0BinderV1
                       MM0RegularBinderKindV1 MM0AnonymousBinderV1
                       (MM0TypeV1 $wff nil)) (cons-atom nil ()))) ())))
                 (MM0TypeV1 $wff nil))
               (NatSuccV1
                 (NatSuccV1 (NatSuccV1 NatZeroV1)))
               MM0RuntimeTextualSourceV1)
             MM0DeclarationEntriesNilV1)))
       $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/term_environment_positive.mm0")
         (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeTermEnvironmentAcceptedV1))

!(let* (($set (mm0:term:name:set))
         ($constant (mm0:term:name:constant)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedPureTermTargetV1
         (NatSuccV1 NatZeroV1) $constant $set)
       $environment $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/term_environment_pure_target.mm0")
         (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimePureTermTargetRefusedV1))

!(let* (($atom (mm0:term:name:atom))
         ($bind (mm0:term:name:bind)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedStrictBoundSortV1
         (NatSuccV1 (NatSuccV1 NatZeroV1)) $bind $atom)
       $environment $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/term_environment_strict_binder.mm0")
         (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeStrictBoundSortRefusedV1))

!(let* (($missing (mm0:term:name:missing))
         ($constant (mm0:term:name:constant)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedUnknownSortV1 NatZeroV1 $constant $missing)
       $environment $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/term_environment_unknown_sort.mm0")
         (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeUnknownSortRefusedV1))

!(let* (($x (mm0:term:name:x))
         ($bad (mm0:term:name:bad)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedUnknownDependencyV1
         (NatSuccV1 (NatSuccV1 NatZeroV1)) $bad $x)
       $environment $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/term_environment_unknown_dependency.mm0")
         (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeUnknownDependencyRefusedV1))

!(let* (($constant (mm0:term:name:constant)))
   (let
     (gslt:result:MM0TextualEnvironmentFromAstV1:1000
       (MM0RefusedTermRedeclarationV1
         (NatSuccV1 (NatSuccV1 NatZeroV1))
         $constant (NatSuccV1 NatZeroV1))
       $environment $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/term_environment_redeclaration.mm0")
         (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeTermRedeclarationRefusedV1))
