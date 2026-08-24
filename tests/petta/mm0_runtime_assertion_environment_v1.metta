!(import! &self langdef)

(= (mm0:assertion:run-ast $ast)
   (collapse
     (gslt:mode:MM0SourceEnvironmentFromAstV1:1000 $ast)))

(= (mm0:assertion:run-file $path)
   (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
     (let
       (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
       (langdef:parse-file $mm0-langdef $path)
       (mm0:assertion:run-ast $ast))))

(= (mm0:assertion:name:th-arrow)
   (cons (cp 116)
     (cons (cp 104)
       (cons (cp 95)
         (cons (cp 97)
           (cons (cp 114)
             (cons (cp 114)
               (cons (cp 111) (cons (cp 119) nil)))))))))

!(let
   ((gslt:result:MM0SourceEnvironmentFromAstV1:1000
      MM0AcceptedV1 $state $history))
   (mm0:assertion:run-file
     "tests/langdef/mm0/assertion_environment_positive.mm0")
   (let
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1 $sorts $declarations) $notation)
     $state
     (let
       (gslt:result:MM0AssertionLookupV1:110
         (MM0AssertionFoundV1 MM0TheoremDeclarationV1 $position))
       (eval
         (quote
           (gslt:fn:MM0AssertionLookupV1:110
             (mm0:assertion:name:th-arrow) $declarations)))
       MM0RuntimeAssertionEnvironmentPositiveAcceptedV1)))

!(let
   ((gslt:result:MM0SourceEnvironmentFromAstV1:1000
      MM0AcceptedV1 $state $history))
   (mm0:assertion:run-file
     "tests/langdef/mm0/assertion_environment_single_axiom.mm0")
   MM0RuntimeAssertionEnvironmentSingleAxiomAcceptedV1)

!(let
   ((gslt:result:MM0SourceEnvironmentFromAstV1:1000
      (MM0RefusedAssertionRedeclarationV1
        $position $name $first-position)
      $state $history))
   (mm0:assertion:run-file
     "tests/langdef/mm0/assertion_environment_redeclaration.mm0")
   MM0RuntimeAssertionEnvironmentRedeclarationRefusedV1)

!(let
   ((gslt:result:MM0SourceEnvironmentFromAstV1:1000
      (MM0RefusedAssertionBinderOrderV1 $position $name)
      $state $history))
   (mm0:assertion:run-file
     "tests/langdef/mm0/assertion_environment_binder_order.mm0")
   MM0RuntimeAssertionEnvironmentBinderOrderRefusedV1)

!(let
   ((gslt:result:MM0SourceEnvironmentFromAstV1:1000
      (MM0RefusedAssertionConclusionTypeV1 $position $name)
      $state $history))
   (mm0:assertion:run-file
     "tests/langdef/mm0/assertion_environment_typed_conclusion.mm0")
   MM0RuntimeAssertionEnvironmentTypedConclusionRefusedV1)
