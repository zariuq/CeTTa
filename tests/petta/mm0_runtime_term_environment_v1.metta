!(import! &self langdef)

(= (mm0:term:name:set)
   (cons (cp 115) (cons (cp 101) (cons (cp 116) nil))))
(= (mm0:term:name:wff)
   (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))))
(= (mm0:term:name:all)
   (cons (cp 97) (cons (cp 108) (cons (cp 108) nil))))
(= (mm0:term:name:imp)
   (cons (cp 105) (cons (cp 109) (cons (cp 112) nil))))
(= (mm0:term:name:x) (cons (cp 120) nil))
(= (mm0:term:name:ph) (cons (cp 112) (cons (cp 104) nil)))
(= (mm0:term:name:constant)
   (cons (cp 99)
     (cons (cp 111)
       (cons (cp 110)
         (cons (cp 115)
           (cons (cp 116)
             (cons (cp 97)
               (cons (cp 110) (cons (cp 116) nil)))))))))
(= (mm0:term:name:atom)
   (cons (cp 97) (cons (cp 116) (cons (cp 111) (cons (cp 109) nil)))))
(= (mm0:term:name:bind)
   (cons (cp 98) (cons (cp 105) (cons (cp 110) (cons (cp 100) nil)))))
(= (mm0:term:name:missing)
   (cons (cp 109)
     (cons (cp 105)
       (cons (cp 115)
         (cons (cp 115)
           (cons (cp 105)
             (cons (cp 110) (cons (cp 103) nil))))))))
(= (mm0:term:name:bad)
   (cons (cp 98) (cons (cp 97) (cons (cp 100) nil))))
(= (mm0:term:name:value)
   (cons (cp 118)
     (cons (cp 97)
       (cons (cp 108)
         (cons (cp 117) (cons (cp 101) nil))))))

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
               (cons
                 (MM0BinderV1
                   MM0BoundBinderKindV1 (MM0NamedBinderV1 $x)
                   (MM0TypeV1 $set nil))
                 (cons
                   (MM0BinderV1
                     MM0RegularBinderKindV1 (MM0NamedBinderV1 $ph)
                     (MM0TypeV1 $wff (cons $x nil)))
                   nil))
               (MM0TypeV1 $wff nil))
             (NatSuccV1 (NatSuccV1 NatZeroV1))
             MM0RuntimeTextualSourceV1)
           (MM0DeclarationEntriesConsV1
             (MM0TermEntryV1
               $imp
               (MM0TermSignatureV1
                 (cons
                   (MM0BinderV1
                     MM0RegularBinderKindV1 MM0AnonymousBinderV1
                     (MM0TypeV1 $wff nil))
                   (cons
                     (MM0BinderV1
                       MM0RegularBinderKindV1 MM0AnonymousBinderV1
                       (MM0TypeV1 $wff nil))
                     nil))
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
         (eval
           (quote
             (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))))
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
         (eval
           (quote
             (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))))
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
         (eval
           (quote
             (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))))
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
         (eval
           (quote
             (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))))
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
         (eval
           (quote
             (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))))
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
         (eval
           (quote
             (gslt:fn:MM0TextualEnvironmentFromAstV1:1000 $ast)))))
     MM0RuntimeTermRedeclarationRefusedV1))
