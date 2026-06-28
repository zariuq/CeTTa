!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)

!(assertEqual (mm-db-summary (mm-mmverify-nested-scope-path)) (mm-mmverify-nested-scope-summary))
