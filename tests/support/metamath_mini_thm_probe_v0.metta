!(import! &self ./lib_parse_metamath_grammar_v0.metta)

!(assertEqual
  (mm-db-summary "/home/aimama/aihub/hyperon/CeTTa/tests/support/metamath_mini_thm_v0.mm")
  (MMDbSummary 11 "const:zero,const:term,const:wff,const:turn,var:t,var:P,hyp:tt,hyp:wp,assert:tze,assert:axp,assert:id"))
