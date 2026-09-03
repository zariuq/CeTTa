!(import! &self langdef)

!(bind! &official-to-named
   (langdef:load-language "langdef/tptp/official_fof_to_named_v1.metta"))
!(assertEqual (langdef:close-language &official-to-named) True)

!(bind! &named-to-resolved
   (langdef:load-language "langdef/tptp/named_fof_to_resolved_v1.metta"))
!(assertEqual (langdef:close-language &named-to-resolved) True)

!(bind! &normalization
   (langdef:load-language "langdef/tptp/fof_normalization_v1.metta"))
!(assertEqual (langdef:close-language &normalization) True)

!(bind! &prenex-normalization
   (langdef:load-language
     "langdef/tptp/fof_prenex_normalization_v1.metta"))
!(assertEqual (langdef:close-language &prenex-normalization) True)

!(bind! &skolemization
   (langdef:load-language "langdef/tptp/fof_skolemization_v1.metta"))
!(assertEqual (langdef:close-language &skolemization) True)

!(bind! &definitional-naming
   (langdef:load-language
     "langdef/tptp/fof_definitional_naming_v1.metta"))
!(assertEqual (langdef:close-language &definitional-naming) True)

!(bind! &definitional-cnf
   (langdef:load-language
     "langdef/tptp/fof_definitional_cnf_generation_v1.metta"))
!(assertEqual (langdef:close-language &definitional-cnf) True)

!(bind! &official-batch-projection
   (langdef:load-language
     "langdef/tptp/official_fof_batch_projection_v1.metta"))
!(assertEqual (langdef:close-language &official-batch-projection) True)

!(bind! &batch-generation
   (langdef:load-language
     "langdef/tptp/fof_clausification_batch_generation_v1.metta"))
!(assertEqual (langdef:close-language &batch-generation) True)

!(bind! &name-allocation
   (langdef:load-language
     "langdef/tptp/fof_cnf_name_allocation_v1.metta"))
!(assertEqual (langdef:close-language &name-allocation) True)

!(bind! &official-cnf-ast
   (langdef:load-language "langdef/tptp/fof_cnf_official_ast_v1.metta"))
!(assertEqual (langdef:close-language &official-cnf-ast) True)

!(println! (TptpFofTransformationLanguageDefsSummary 11 11 0))
