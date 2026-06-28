!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)

!(assertEqual
  (gparse:glr-class (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g proc (rho-send-nil-toks))
  Unique)

!(assertEqual
  (gparse:glr-class (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g proc (rho-bad-toks))
  NoParse)

!(assertEqual
  (gparse:glr-class (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta") rho-g-ambig proc (mkL (x BAR x BAR x)))
  Ambiguous)

!(assertEqual
  (gparse:glr-class (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta") mm-g stmt (mm-cdecl-toks))
  Unique)
