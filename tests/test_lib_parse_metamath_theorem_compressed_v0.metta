!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (lp-parse-class (mm-g) stmt (mm-compressed-thm-toks)) Unique)
!(assertEqual (lp-parse-class (mm-g) stmt (mm-empty-compressed-thm-toks)) Unique)
!(assertEqual (lp-parse-class (mm-g) stmt (mm-bad-compressed-toks)) NoParse)
