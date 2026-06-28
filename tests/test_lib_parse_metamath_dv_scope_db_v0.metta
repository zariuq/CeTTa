!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mmverify-dv-scope-path)) (mm-mmverify-dv-scope-summary))
