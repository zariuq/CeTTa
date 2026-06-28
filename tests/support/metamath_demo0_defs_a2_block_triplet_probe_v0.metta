!(import! &self ./lib_parse_metamath_grammar_v0.metta)

!(assertEqual
  (mm-db-summary "/home/aimama/aihub/hyperon/CeTTa/tests/support/metamath_demo0_defs_a2_block_triplet_v0.mm")
  (MMDbSummary 27 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim,assert:a2,hyp:min,hyp:maj,assert:mp"))
