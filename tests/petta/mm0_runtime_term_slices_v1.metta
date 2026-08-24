!(import! &self langdef)

!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/term_environment_constant_positive.mm0")
     (collapse
       (eval
         (quote
           (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast))))))

!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/term_environment_bound_positive.mm0")
     (collapse
       (eval
         (quote
           (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast))))))

!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/term_environment_dependency_positive.mm0")
     (collapse
       (eval
         (quote
           (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast))))))

!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/term_environment_arrow_positive.mm0")
     (collapse
       (eval
         (quote
           (gslt:mode:MM0TextualEnvironmentFromAstV1:1000 $ast))))))
