!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self langdef)
!(import! &self system)

(= (fixture:document)
   (tptp:read-text-document
     "cnf(c,axiom,(p(42)|~q(\"d\")),inference(rule,[],[])).\n"))

(= (gate:run $source-path $document)
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/tptp/official_compact_lexical_value_v1.metta"
            $source-path))
     (case $program
       (((NativeHandle $_kind $_id)
          (let $answers
               (collapse
                 (langdef:run-deterministic-equations $program
                   (LangDef:DeterministicCall
                     tptp-view-v1:clause-data $document)))
            (let $_ (langdef:close-deterministic-equations $program)
              (superpose $answers))))
        ($fault $fault)))))

(= (gate:disagrees $reference $mutant)
   (case (== $reference $mutant)
     ((True False)
      (False True))))

!(println!
   (TptpCompactFirstOrderViewMutationV1
     (gate:disagrees
       (gate:run (system:arg 0) (fixture:document))
       (gate:run (system:arg 1) (fixture:document)))))
