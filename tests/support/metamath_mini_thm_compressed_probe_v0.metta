!(import! &self ./lib_parse_metamath_grammar_v0.metta)

!(assertEqual
  (mm-db-summary "/home/aimama/aihub/hyperon/CeTTa/tests/support/metamath_mini_thm_compressed_v0.mm")
  (MMDbSummary 10 "const:wff,const:lp,const:rp,const:impl,var:ph,var:ps,hyp:wph,hyp:wps,assert:wi,assert:th1"))
