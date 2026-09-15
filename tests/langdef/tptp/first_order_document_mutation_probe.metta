!(import! &self langdef)

!(bind! &plan
   (langdef:load-deterministic-equations
     (LangDef:EquationSources
       "langdef/tptp/cnf_clause_data_bridge_v1.metta"
       "@DOCUMENT_BRIDGE@")))
!(println!
   (TptpFirstOrderDocumentIffMutationProbe
     (__cetta_lib_deterministic_equations_run &plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node iff
           (pair
             (node atomic
               (node constant
                 (node lower-word (pair (cp 112) nil))))
             (node atomic
               (node constant
                 (node lower-word (pair (cp 113) nil))))))
         scope))))
!(println!
   (TptpFirstOrderDocumentExistsMutationProbe
     (__cetta_lib_deterministic_equations_run &plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node exists
           (pair
             (node variable-list
               (pair (node upper-word (pair (cp 88) nil)) nil))
             (node atomic
               (node constant
                 (node lower-word (pair (cp 112) nil))))))
         scope))))
!(assertEqual (langdef:close-deterministic-equations &plan) True)
