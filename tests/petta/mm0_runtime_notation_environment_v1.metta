!(import! &self langdef)

(= (mm0:notation:name:wff)
   (cons-atom cons (cons-atom (cp 119) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom (cons-atom cons (cons-atom (cp 102) (cons-atom nil ()))) ()))) ()))))
(= (mm0:notation:name:neg)
   (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 101) (cons-atom (cons-atom cons (cons-atom (cp 103) (cons-atom nil ()))) ()))) ()))))
(= (mm0:notation:name:imp)
   (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 109) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom nil ()))) ()))) ()))))
(= (mm0:notation:name:app)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom nil ()))) ()))) ()))))
(= (mm0:notation:name:missing)
   (cons-atom cons (cons-atom (cp 109) (cons-atom (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 115) (cons-atom (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 103) (cons-atom nil ()))) ()))) ()))) ()))) ()))) ()))) ()))))
(= (mm0:notation:name:unary)
   (cons-atom cons (cons-atom (cp 117) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 114) (cons-atom (cons-atom cons (cons-atom (cp 121) (cons-atom nil ()))) ()))) ()))) ()))) ()))))
(= (mm0:notation:name:binary)
   (cons-atom cons (cons-atom (cp 98) (cons-atom (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 114) (cons-atom (cons-atom cons (cons-atom (cp 121) (cons-atom nil ()))) ()))) ()))) ()))) ()))) ()))))

(= (mm0:notation:token:tilde)
   (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))
(= (mm0:notation:token:arrow)
   (MM0MathScalarsConsV1
     (cp 45) (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
(= (mm0:notation:token:at)
   (MM0MathScalarsConsV1 (cp 64) MM0MathScalarsNilV1))

!(let*
       (($wff (mm0:notation:name:wff))
        ($neg (mm0:notation:name:neg))
        ($imp (mm0:notation:name:imp))
        ($app (mm0:notation:name:app))
        ($tilde (mm0:notation:token:tilde))
        ($arrow (mm0:notation:token:arrow))
        ($at (mm0:notation:token:at)))
       (let
         (gslt:result:MM0SourceEnvironmentFromAstV1:1000
           MM0AcceptedV1
           (MM0SourceStateV1
             (MM0TextualEnvironmentV1 $sorts $declarations)
             (MM0NotationEnvironmentV1
               $delimiters
               (MM0ConstantEntriesConsV1
                 (MM0ConstantEntryV1
                   $tilde
                   (MM0FinitePrecedenceV1
                     (cons-atom cons (cons-atom (cp 53) (cons-atom (cons-atom cons (cons-atom (cp 48) (cons-atom nil ()))) ()))))
                   (NatSuccV1
                     (NatSuccV1
                       (NatSuccV1 (NatSuccV1 NatZeroV1)))))
                 (MM0ConstantEntriesConsV1
                   (MM0ConstantEntryV1
                     $arrow
                     (MM0FinitePrecedenceV1
                       (cons-atom cons (cons-atom (cp 50) (cons-atom (cons-atom cons (cons-atom (cp 48) (cons-atom nil ()))) ()))))
                     (NatSuccV1
                       (NatSuccV1
                         (NatSuccV1
                           (NatSuccV1 (NatSuccV1 NatZeroV1))))))
                   (MM0ConstantEntriesConsV1
                     (MM0ConstantEntryV1
                       $at MM0MaximumPrecedenceV1
                       (NatSuccV1
                         (NatSuccV1
                           (NatSuccV1
                             (NatSuccV1
                               (NatSuccV1
                                 (NatSuccV1 NatZeroV1)))))))
                     MM0ConstantEntriesNilV1)))
               (MM0PrefixEntriesConsV1
                 (MM0PrefixNotationEntryV1
                   $tilde $neg (NatSuccV1 NatZeroV1)
                   (MM0FinitePrecedenceV1
                     (cons-atom cons (cons-atom (cp 53) (cons-atom (cons-atom cons (cons-atom (cp 48) (cons-atom nil ()))) ()))))
                   (NatSuccV1
                     (NatSuccV1
                       (NatSuccV1 (NatSuccV1 NatZeroV1)))))
                 (MM0PrefixEntriesConsV1
                   (MM0PrefixNotationEntryV1
                     $at $app (NatSuccV1 NatZeroV1)
                     MM0MaximumPrecedenceV1
                     (NatSuccV1
                       (NatSuccV1
                         (NatSuccV1
                           (NatSuccV1
                             (NatSuccV1
                               (NatSuccV1 NatZeroV1)))))))
                   MM0PrefixEntriesNilV1))
               (MM0InfixEntriesConsV1
                 (MM0InfixNotationEntryV1
                   $arrow $imp MM0RightAssociativeV1
                   (MM0FinitePrecedenceV1
                     (cons-atom cons (cons-atom (cp 50) (cons-atom (cons-atom cons (cons-atom (cp 48) (cons-atom nil ()))) ()))))
                   (NatSuccV1
                     (NatSuccV1
                       (NatSuccV1
                         (NatSuccV1 (NatSuccV1 NatZeroV1))))))
                 MM0InfixEntriesNilV1)))
           $history)
         (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
           (let
             (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
             (langdef:parse-file $mm0-langdef
               "tests/langdef/mm0/notation_environment_positive.mm0")
             (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
       MM0RuntimeNotationEnvironmentAcceptedV1))

!(let $missing (mm0:notation:name:missing)
   (let
     (gslt:result:MM0SourceEnvironmentFromAstV1:1000
       (MM0RefusedUnknownNotationTermV1 $position $missing)
       $state $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/notation_environment_unknown_term.mm0")
         (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeNotationUnknownTermRefusedV1))

!(let $unary (mm0:notation:name:unary)
   (let
     (gslt:result:MM0SourceEnvironmentFromAstV1:1000
       (MM0RefusedInfixArityV1 $position $unary (NatSuccV1 NatZeroV1))
       $state $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/notation_environment_infix_arity.mm0")
         (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeNotationInfixArityRefusedV1))

!(let $binary (mm0:notation:name:binary)
   (let
     (gslt:result:MM0SourceEnvironmentFromAstV1:1000
       (MM0RefusedInfixMaximumPrecedenceV1 $position $binary)
       $state $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/notation_environment_infix_max.mm0")
         (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeNotationInfixMaximumRefusedV1))

!(let $tilde (mm0:notation:token:tilde)
   (let
     (gslt:result:MM0SourceEnvironmentFromAstV1:1000
       (MM0RefusedNotationPrecedenceConflictV1
         $position $tilde $old-precedence $new-precedence)
       $state $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/notation_environment_precedence_conflict.mm0")
         (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeNotationPrecedenceConflictRefusedV1))

!(let $tilde (mm0:notation:token:tilde)
   (let
     (gslt:result:MM0SourceEnvironmentFromAstV1:1000
       (MM0RefusedNotationRedeclarationV1
         $position $tilde $first-position)
       $state $history)
     (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
       (let
         (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
         (langdef:parse-file $mm0-langdef
           "tests/langdef/mm0/notation_environment_redeclaration.mm0")
         (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
     MM0RuntimeNotationRedeclarationRefusedV1))
