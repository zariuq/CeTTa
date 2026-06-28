!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(println! (gparse:slr-parse-shared "/home/aimama/aihub/hyperon/CeTTa/tests/support/lib_parse_metamath_grammar_v0.metta" mm-g stmt (mm-cdecl-toks)))
!(println! (gparse:slr-parse-shared "/home/aimama/aihub/hyperon/CeTTa/tests/support/lib_parse_metamath_grammar_v0.metta" mm-g stmt (mm-empty-compressed-thm-toks)))
