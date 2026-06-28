!(import! &self ./lib_parse_metamath_grammar_v0.metta)

!(println! "mini-block")
!(println! (mm-db-parse-class "/home/aimama/aihub/hyperon/CeTTa/tests/support/metamath_mini_block_v0.mm"))

!(println! "mini-block-thm")
!(println! (mm-db-parse-class "/home/aimama/aihub/hyperon/CeTTa/tests/support/metamath_mini_block_thm_v0.mm"))

!(println! "demo0-theorem-noblock")
!(println! (mm-db-parse-class "/home/aimama/aihub/hyperon/CeTTa/tests/support/metamath_demo0_theorem_noblock_v0.mm"))
