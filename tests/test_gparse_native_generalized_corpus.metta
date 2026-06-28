!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)

(= (shared-parse-class $res)
   (case $res
     (((Unique $cert) Unique)
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o Error))))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/surface_shadowing.rho"))
  (Unique))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/surface_shadowing.rho")))
  (Unique))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/surface_name_output.rho"))
  (Unique))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/surface_name_output.rho")))
  (Unique))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/surface_open_name_variable.rho"))
  (Unique))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/surface_open_name_variable.rho")))
  (Unique))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/surface_quote_seals_outer_binder.rho"))
  (Unique))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/surface_quote_seals_outer_binder.rho")))
  (Unique))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/reject_quoted_payload.rho"))
  (NoParse))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/reject_quoted_payload.rho")))
  (NoParse))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/rotating_scheduler_persistent_branch.rho"))
  (Ambiguous))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/rotating_scheduler_persistent_branch.rho")))
  (Ambiguous))

!(assertEqualToResult
  (gparse:glr-class
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-lex-file->toks "tests/rhocalc/rotating_scheduler_primed_diamond.rho"))
  (Ambiguous))

!(assertEqualToResult
  (shared-parse-class
    (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-lex-file->toks "tests/rhocalc/rotating_scheduler_primed_diamond.rho")))
  (Ambiguous))
