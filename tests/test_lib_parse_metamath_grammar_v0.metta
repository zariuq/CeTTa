!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

; Core bounded Metamath grammar-data lane:
; lexical classes + declaration/floating/theorem statement shapes + one trusted cert.

!(assertEqual (lp-admit (mm-g)) Admitted)
!(assertEqual (lp-admit (mkGram ((LexD mm-word a)(LexD mm-word b)))) (Rejected dup-var))
!(assertEqual (lp-parse-class (mm-g) stmt (mm-cdecl-toks)) Unique)
!(assertEqual (lp-parse-class (mm-g) stmt (mm-vdecl-toks)) Unique)
!(assertEqual (lp-parse-class (mm-g) stmt (mm-float-toks)) Unique)
!(assertEqual (lp-parse-class (mm-g) stmt (mm-dv-toks)) Unique)
!(assertEqual (lp-parse-cert
                (mm-g)
                stmt
                (mm-float-toks)
                (pu-tree (lp-parse (mm-g) stmt (mm-float-toks))))
              True)
