!(import! &self langdef)
!(let $handle (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
   (progn
     (println! (collapse
       (gslt:admitted-entry:AdmittedIntegerAlignmentV1:10 $handle
         (bnf-v1:scalars-cons 0
           (bnf-v1:scalars-cons 1 (bnf-v1:scalars-nil))))))
     (langdef:close-language $handle)))
