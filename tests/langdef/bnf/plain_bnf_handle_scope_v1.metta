!(import! &self langdef)

; The host observer checks actual allocations and retirement before context
; teardown. These are genuinely zero-result continuations, not empty packets.
(= (typed-lifetime:probe zero-language)
   (let (NativeHandle $kind $id)
        (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
     (superpose ())))
(= (typed-lifetime:probe zero-program)
   (let (NativeHandle $kind $id)
        (langdef:load-deterministic-equations
          (LangDef:EquationSources "langdef/bnf/plain_bnf_cst_projection_v1.metta"))
     (superpose ())))
(= (typed-lifetime:probe zero-parser)
   (let (NativeHandle $kind $id)
        (langdef:load-authored-parser "langdef/bnf/plain_bnf_source_v1.metta"
          "langdef/bnf/plain_bnf_parser_profile_v1.metta")
     (superpose ())))
!(typed-lifetime:probe zero-language)
!(typed-lifetime:probe zero-program)
!(typed-lifetime:probe zero-parser)

; Retain a parser as data in a Space across top-level evaluation boundaries.
(= (typed-lifetime:probe stored-parser)
   (let $parser
        (langdef:load-authored-parser "langdef/bnf/plain_bnf_source_v1.metta"
          "langdef/bnf/plain_bnf_parser_profile_v1.metta")
     (let $_ (add-atom &self (HandleScopeParserV1 $parser))
       (superpose (HandleScopeStoredV1 HandleScopeStoredV1)))))
!(typed-lifetime:probe stored-parser)
!(match &self (HandleScopeParserV1 $parser)
   (case (langdef:parse-authored-text $parser "<s> ::= \"x\"\n")
     (((LangDef:AuthoredParseAccepted PlainBnfSourceV1 $_binding $_source ($tree))
        HandleScopeEscapedParserWorksV1)
      ($unexpected (HandleScopeUnexpectedV1 $unexpected)))))
!(match &self (HandleScopeParserV1 $parser)
   (HandleScopeEarlyCloseV1 (langdef:close-authored-parser $parser)))
!(match &self (HandleScopeParserV1 $parser)
   (HandleScopeSecondCloseV1 (langdef:close-authored-parser $parser)))
