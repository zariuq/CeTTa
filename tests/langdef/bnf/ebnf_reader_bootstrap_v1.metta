!(import! &self ../../../lib/lib_bnf.metta)

!(let $parser
       (bnf:parser-from-file-with-authority
         "langdef/bnf/ebnf_source_v1.bnf" (bnf:self-authority-v1))
   (case $parser
     (((NativeHandle $kind $id)
        (let $parsed
             (langdef:parse-authored-text $parser
               "<s> ::= (\"λ\" | \"雪\")* \"!\"? <s>+\n")
          (let $_ (langdef:close-authored-parser $parser)
            (case $parsed
              (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                  $binding $source ($cst))
                 (let $projection
                      (langdef:load-deterministic-equations
                        (LangDef:EquationSources
                          "langdef/bnf/ebnf_cst_projection_v1.metta"))
                   (let $doc
                        (langdef:run-deterministic-equations $projection
                          (LangDef:DeterministicCall ebnf-v1:project $cst))
                     (let $_ (langdef:close-deterministic-equations $projection)
                       (let $language
                            (langdef:load-language "langdef/bnf/ebnf_grammar_v1.metta")
                         (let $admitted
                              (langdef:admit-term $language "BnfGrammarDocument" $doc)
                           (let $_ (langdef:close-language $language)
                             ($admitted $doc))))))))
               ($unexpected (EbnfUnexpectedParseV1 $unexpected)))))))
      ($unexpected (EbnfUnexpectedReaderV1 $unexpected)))))
